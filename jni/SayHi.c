/**
 *
 * Copyright (C) 2013 ALiang (illuspas@gmail.com)
 *
 * Licensed under the GPLv2 license. See 'COPYING' for more details.
 *
 */
#include <jni.h>
#include <assert.h>
#include <string.h>

// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

// for native asset manager
#include <sys/types.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <android/log.h>
#include <pthread.h>

#include <librtmp/rtmp.h>
#include <speex/speex.h>
#include <speex/speex_header.h>

#include <librtmp/rtmp.h>
#include <librtmp/log.h>

#define LOG_TAG "Say.NDK"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))

// engine interfaces
static SLObjectItf engineObject = NULL;
static SLEngineItf engineEngine;

// recorder interfaces
static SLObjectItf recorderObject = NULL;
static SLRecordItf recorderRecord;
static SLAndroidSimpleBufferQueueItf recorderBufferQueue;

// 1 seconds of recorded audio at 16 kHz mono, 16-bit signed little endian
#define RECORDER_FRAMES 16000
static short recorderBuffer[RECORDER_FRAMES];
static unsigned recorderSize = 0;
static SLmilliHertz recorderSR;

// output mix interfaces
static SLObjectItf outputMixObject = NULL;
static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;

// buffer queue player interfaces
static SLObjectItf bqPlayerObject = NULL;
static SLPlayItf bqPlayerPlay;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
static SLEffectSendItf bqPlayerEffectSend;
static SLMuteSoloItf bqPlayerMuteSolo;
static SLVolumeItf bqPlayerVolume;

static short playerBuffer[RECORDER_FRAMES];
static unsigned playerSize = 0;

JavaVM *gJvm = NULL;
jobject gObj = NULL;
jmethodID eventMid;
pthread_attr_t attr;

//publisher variable
char* pubRtmpUrl;
pthread_t openPublisherPid;
pthread_mutex_t recodMutex;
pthread_cond_t recodCond;
int isOpenPub;
int isStartPub;

SpeexBits ebits; //speex
int enc_frame_size;
void *enc_state;
RTMP *pubRtmp; //rtmp
const char speex_head = 0xb6;
int isPubConnected;
uint32_t ts;

//player variable
SpeexBits dbits;
int dec_frame_size;
void *dec_state;

char* playRtmpUrl;
int isOpenPlay;

pthread_t openPlayerPid;
pthread_mutex_t playMutex;
pthread_cond_t playCond;

int isOpenPlay;
int isStartPlay;

RTMP *playRtmp; //rtmp
int dec_frame_size; //speex
SpeexBits dbits;
void *dec_state;

int bigFourByteToInt(char* bytes)
{
    int num = 0;
    num += (int) bytes[0] << 24;
    num += (int) bytes[1] << 16;
    num += (int) bytes[2] << 8;
    num += (int) bytes[3];
    return num;
}

int bigThreeByteToInt(char* bytes)
{
    int num = 0;
    num += (int) bytes[0] << 16;
    num += (int) bytes[1] << 8;
    num += (int) bytes[2];
    return num;
}

int bigTwoByteToInt(char* bytes)
{
    int num = 0;
    num += (int) bytes[0] << 8;
    num += (int) bytes[1];
    return num;
}

void send_pkt(char* buf, int buflen, int type, unsigned int timestamp)
{
    int ret;
    RTMPPacket rtmp_pakt;
    RTMPPacket_Reset(&rtmp_pakt);
    RTMPPacket_Alloc(&rtmp_pakt, buflen);
    rtmp_pakt.m_packetType = type;
    rtmp_pakt.m_nBodySize = buflen;
    rtmp_pakt.m_nTimeStamp = timestamp;
    rtmp_pakt.m_nChannel = 4;
    rtmp_pakt.m_headerType = RTMP_PACKET_SIZE_LARGE;
    rtmp_pakt.m_nInfoField2 = pubRtmp->m_stream_id;
    memcpy(rtmp_pakt.m_body, buf, buflen);
    ret = RTMP_SendPacket(pubRtmp, &rtmp_pakt, 0);
    RTMPPacket_Free(&rtmp_pakt);
}

void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
    pthread_mutex_lock(&recodMutex);
    LOGI("bqRecorderCallback\n");
    pthread_cond_signal(&recodCond);
    pthread_mutex_unlock(&recodMutex);
}

int initNativeRecoder()
{
    // create native audio recoder
    SLresult result;
    // configure audio source
    SLDataLocator_IODevice loc_dev = { SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT, SL_DEFAULTDEVICEID_AUDIOINPUT, NULL };
    SLDataSource audioSrc = { &loc_dev, NULL };
    // configure audio sink
    SLDataLocator_AndroidSimpleBufferQueue loc_bq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    SLDataFormat_PCM format_pcm = { SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN };
    SLDataSink audioSnk = { &loc_bq, &format_pcm };
    // create audio recorder
    // (requires the RECORD_AUDIO permission)
    const SLInterfaceID id[1] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
    const SLboolean req[1] = { SL_BOOLEAN_TRUE };
    result = (*engineEngine)->CreateAudioRecorder(engineEngine, &recorderObject, &audioSrc, &audioSnk, 1, id, req);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    // realize the audio recorder
    result = (*recorderObject)->Realize(recorderObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    // get the record interface
    result = (*recorderObject)->GetInterface(recorderObject, SL_IID_RECORD, &recorderRecord);
    assert(SL_RESULT_SUCCESS == result);
    // get the buffer queue interface
    result = (*recorderObject)->GetInterface(recorderObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &recorderBufferQueue);
    assert(SL_RESULT_SUCCESS == result);
    // register callback on the buffer queue
    result = (*recorderBufferQueue)->RegisterCallback(recorderBufferQueue, bqRecorderCallback, NULL);
    assert(SL_RESULT_SUCCESS == result);

    // in case already recording, stop recording and clear buffer queue
    result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_STOPPED);
    assert(SL_RESULT_SUCCESS == result);
    result = (*recorderBufferQueue)->Clear(recorderBufferQueue);
    assert(SL_RESULT_SUCCESS == result);
    // the buffer is not valid for playback yet
    recorderSize = 0;

    // start recording
    result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_RECORDING);
    assert(SL_RESULT_SUCCESS == result);
    LOGI("startRecoding success \n");
    return JNI_TRUE;
}

void* openPubliserThread(void* args)
{
    JNIEnv *env;
    SLresult result;
    int i, enc_size;
    short* pcm_buffer;
    char* output_buffer;
    int compression = 2;
    int sample_rate;
    isOpenPub = 1;
    (*gJvm)->AttachCurrentThread(gJvm, &env, NULL);
    pthread_mutex_init(&recodMutex, NULL);
    pthread_cond_init(&recodCond, NULL);
    do {
        (*env)->CallVoidMethod(env, gObj, eventMid, 1); //Start init native audio recoder

        pubRtmp = RTMP_Alloc();
        RTMP_Init(pubRtmp);
        LOGI("RTMP_Init %s\n", pubRtmpUrl);
        if (!RTMP_SetupURL(pubRtmp, pubRtmpUrl)) {
            LOGI("RTMP_SetupURL error\n");
            break;
        }
        RTMP_EnableWrite(pubRtmp);
        LOGI("RTMP_EnableWrite\n");
        if (!RTMP_Connect(pubRtmp, NULL) || !RTMP_ConnectStream(pubRtmp, 0)) {
            LOGI("RTMP_Connect or RTMP_ConnectStream error\n");
            break;
        }
        LOGI("RTMP_Connected\n");
        if (!initNativeRecoder()) {
            (*env)->CallVoidMethod(env, gObj, eventMid, 2);
            break;
        }

        //init speex encoder
        speex_bits_init(&ebits);
        enc_state = speex_encoder_init(&speex_wb_mode);
        speex_encoder_ctl(enc_state, SPEEX_SET_QUALITY, &compression);
        speex_encoder_ctl(enc_state, SPEEX_GET_FRAME_SIZE, &enc_frame_size);
        speex_encoder_ctl(enc_state, SPEEX_GET_SAMPLING_RATE, &sample_rate);
        pcm_buffer = malloc(enc_frame_size * sizeof(short));
        output_buffer = malloc(enc_frame_size * sizeof(char));
        LOGI( "Speex Encoder init,enc_frame_size:%d sample_rate:%d\n", enc_frame_size, sample_rate);

        (*env)->CallVoidMethod(env, gObj, eventMid, 3);
        isStartPub = 1;
        struct timeval start, end;
        while (isStartPub) {
            pthread_mutex_lock(&recodMutex);
            // enqueue an empty buffer to be filled by the recorder
            // (for streaming recording, we would enqueue at least 2 empty buffers to start things off)
            result = (*recorderBufferQueue)->Enqueue(recorderBufferQueue, recorderBuffer, RECORDER_FRAMES * sizeof(short));
            // the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
            // which for this code example would indicate a programming error
            assert(SL_RESULT_SUCCESS == result);
            pthread_cond_wait(&recodCond, &recodMutex);
            pthread_mutex_unlock(&recodMutex);
            LOGI("Got the audio buffer");
            //   gettimeofday(&start, NULL);
            for (i = 0; i < RECORDER_FRAMES; i += enc_frame_size) {
                //            LOGI("encode position:%d\n",i);
                speex_bits_reset(&ebits);
                memcpy(pcm_buffer, recorderBuffer + i, enc_frame_size * sizeof(short));
                speex_encode_int(enc_state, pcm_buffer, &ebits);
                enc_size = speex_bits_write(&ebits, output_buffer, enc_frame_size);
                //      LOGI("enc_size:%d\n",enc_size);
                char* send_buf = malloc(enc_size + 1);
                memcpy(send_buf, &speex_head, 1);
                memcpy(send_buf + 1, output_buffer, enc_size);
                send_pkt(send_buf, enc_size + 1, RTMP_PACKET_TYPE_AUDIO, ts += 20);
                free(send_buf);
            }
            //   gettimeofday(&end, NULL);
            //  int timeuse = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
            //  LOGI("Encoding finish timeuse:%d\n", timeuse);
        }
        result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_STOPPED);
        if (SL_RESULT_SUCCESS == result) {
            LOGI("SetRecordState SL_RECORDSTATE_STOPPED!\n");
        }
        (*env)->CallVoidMethod(env, gObj, eventMid, 4);

        free(pcm_buffer);
        free(output_buffer);
        speex_bits_destroy(&ebits);
        speex_encoder_destroy(enc_state);
    } while (0);
    if (RTMP_IsConnected(pubRtmp)) {
        RTMP_Close(pubRtmp);
    }
    RTMP_Free(pubRtmp);
    free(pubRtmpUrl);
    (*gJvm)->DetachCurrentThread(gJvm);
    isOpenPub = 0;
}

// this callback handler is called every time a buffer finishes playing
void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
    LOGI("bqPlayerCallback\n");
}

// create buffer queue audio player
void initNativePlayer()
{
    SLresult result;

    // configure audio source
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    SLDataFormat_PCM format_pcm = { SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN };
    SLDataSource audioSrc = { &loc_bufq, &format_pcm };

    // configure audio sink
    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, outputMixObject };
    SLDataSink audioSnk = { &loc_outmix, NULL };

    // create audio player
    const SLInterfaceID ids[3] = { SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND, /*SL_IID_MUTESOLO,*/SL_IID_VOLUME };
    const SLboolean req[3] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, /*SL_BOOLEAN_TRUE,*/SL_BOOLEAN_TRUE };
    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk, 3, ids, req);
    assert(SL_RESULT_SUCCESS == result);

    // realize the player
    result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);

    // get the play interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
    assert(SL_RESULT_SUCCESS == result);

    // get the buffer queue interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE, &bqPlayerBufferQueue);
    assert(SL_RESULT_SUCCESS == result);

    // register callback on the buffer queue
    result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, NULL);
    assert(SL_RESULT_SUCCESS == result);

    // get the effect send interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_EFFECTSEND, &bqPlayerEffectSend);
    assert(SL_RESULT_SUCCESS == result);

#if 0   // mute/solo is not supported for sources that are known to be mono, as this is
    // get the mute/solo interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_MUTESOLO, &bqPlayerMuteSolo);
    assert(SL_RESULT_SUCCESS == result);
#endif

    // get the volume interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
    assert(SL_RESULT_SUCCESS == result);

    // set the player's state to playing
    result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
    assert(SL_RESULT_SUCCESS == result);

}
int playerBufferIndex = 0;
void putAudioQueue(short* data, int dataSize)
{
   // LOGI("putAudioQueue");
    memcpy(playerBuffer+playerBufferIndex,data,dataSize*sizeof(short));
    playerBufferIndex+=dataSize;
    LOGI("playerBufferIndex %d   all:%d",playerBufferIndex,RECORDER_FRAMES);
    if(playerBufferIndex == RECORDER_FRAMES)
    {
        SLresult result;
        result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, playerBuffer, RECORDER_FRAMES * sizeof(short));
        playerBufferIndex = 0;
        LOGI("Enqueue player buffer");
    }
}


void* openPlayerThread(void* args)
{
    isOpenPlay = 1;
    short *output_buffer;
    do {
        playRtmp = RTMP_Alloc();
        RTMP_Init(playRtmp);
        LOGI("Play RTMP_Init %s\n", playRtmpUrl);
        if (!RTMP_SetupURL(playRtmp, playRtmpUrl)) {
            LOGI("Play RTMP_SetupURL error\n");
            break;
        }
        if (!RTMP_Connect(playRtmp, NULL) || !RTMP_ConnectStream(playRtmp, 0)) {
            LOGI("Play RTMP_Connect or RTMP_ConnectStream error\n");
            break;
        }
        LOGI("RTMP_Connected\n");

        //TODO 初始化 opensl es
        initNativePlayer();
        //TODO 初始化speex解码器
        speex_bits_init(&dbits);
        dec_state = speex_decoder_init(&speex_wb_mode);
        speex_decoder_ctl(dec_state, SPEEX_GET_FRAME_SIZE, &dec_frame_size);
        output_buffer = malloc(dec_frame_size * sizeof(short));

        RTMPPacket rtmp_pakt = { 0 };
        isStartPlay = 1;
        while (isStartPlay && RTMP_ReadPacket(playRtmp, &rtmp_pakt)) {
            if (RTMPPacket_IsReady(&rtmp_pakt)) {
                if (!rtmp_pakt.m_nBodySize)
                    continue;
                if (rtmp_pakt.m_packetType == RTMP_PACKET_TYPE_AUDIO) {
                    // 处理音频数据包
                   // LOGI("AUDIO audio size:%d  head:%d  time:%d\n", rtmp_pakt.m_nBodySize, rtmp_pakt.m_body[0], rtmp_pakt.m_nTimeStamp);
                    speex_bits_read_from(&dbits, rtmp_pakt.m_body + 1, rtmp_pakt.m_nBodySize - 1);
                    speex_decode_int(dec_state, &dbits, output_buffer);
                    putAudioQueue(output_buffer,dec_frame_size);
                } else if (rtmp_pakt.m_packetType == RTMP_PACKET_TYPE_VIDEO) {
                    // 处理视频数据包
                } else if (rtmp_pakt.m_packetType == RTMP_PACKET_TYPE_INFO) {
                    // 处理信息包
                } else if (rtmp_pakt.m_packetType == RTMP_PACKET_TYPE_FLASH_VIDEO) {
                    // 其他数据
                    int index = 0;
                    while (1) {
                        int StreamType; //1-byte
                        int MediaSize; //3-byte
                        int TiMMER; //3-byte
                        int Reserve; //4-byte
                        char* MediaData; //MediaSize-byte
                        int TagLen; //4-byte

                        StreamType = rtmp_pakt.m_body[index];
                        index += 1;
                        MediaSize = bigThreeByteToInt(rtmp_pakt.m_body + index);
                        index += 3;
                        TiMMER = bigThreeByteToInt(rtmp_pakt.m_body + index);
                        index += 3;
                        Reserve = bigFourByteToInt(rtmp_pakt.m_body + index);
                        index += 4;
                        MediaData = rtmp_pakt.m_body + index;
                        index += MediaSize;
                        TagLen = bigFourByteToInt(rtmp_pakt.m_body + index);
                        index += 4;
                        //LOGI("bodySize:%d   index:%d",rtmp_pakt.m_nBodySize,index);
                        //LOGI("StreamType:%d MediaSize:%d  TiMMER:%d TagLen:%d\n", StreamType, MediaSize, TiMMER, TagLen);
                        if (StreamType == 0x08) {
                            //音频包
                            //int MediaSize = bigThreeByteToInt(rtmp_pakt.m_body+1);
                          //  LOGI("FLASH audio size:%d  head:%d time:%d\n", MediaSize, MediaData[0], TiMMER);
                            speex_bits_read_from(&dbits, MediaData + 1, MediaSize - 1);
                            speex_decode_int(dec_state, &dbits, output_buffer);
                            putAudioQueue(output_buffer,dec_frame_size);
                        } else if (StreamType == 0x09) {
                            //视频包
                            //  LOGI( "video size:%d  head:%d\n", MediaSize, MediaData[0]);
                        }
                        if (rtmp_pakt.m_nBodySize == index) {
                       //     LOGI("one pakt over\n");
                            break;
                        }
                    }
                }
                //  LOGI( "rtmp_pakt size:%d  type:%d\n", rtmp_pakt.m_nBodySize, rtmp_pakt.m_packetType);
                RTMPPacket_Free(&rtmp_pakt);
            }
        }
    } while (0);
    if (RTMP_IsConnected(playRtmp)) {
        RTMP_Close(playRtmp);
    }
    RTMP_Free(playRtmp);
    free(output_buffer);
    isOpenPlay = 0;
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    Init
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_Init(JNIEnv *env, jobject jobj)
{
    SLresult result;

    // create engine
    result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    assert(SL_RESULT_SUCCESS == result);
    // realize the engine
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);
    // get the engine interface, which is needed in order to create other objects
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    assert(SL_RESULT_SUCCESS == result);

    // create output mix, with environmental reverb specified as a non-required interface
    const SLInterfaceID ids[1] = { SL_IID_ENVIRONMENTALREVERB };
    const SLboolean req[1] = { SL_BOOLEAN_FALSE };
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, ids, req);
    assert(SL_RESULT_SUCCESS == result);

    // realize the output mix
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);

    // get the environmental reverb interface
    // this could fail if the environmental reverb effect is not available,
    // either because the feature is not present, excessive CPU load, or
    // the required MODIFY_AUDIO_SETTINGS permission was not requested and granted
//    result = (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB, &outputMixEnvironmentalReverb);
//    if (SL_RESULT_SUCCESS == result) {
//        result = (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(outputMixEnvironmentalReverb, &reverbSettings);
//    }

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    LOGI("Java_cn_cloudstep_sayhi_SayHi_Init");
    eventMid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, jobj), "onEventCallback", "(I)V");
    (*env)->GetJavaVM(env, &gJvm);
    gObj = (*env)->NewGlobalRef(env, jobj);
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    OpenPublisher
 * Signature: (Ljava/lang/String{})V
 */

JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_OpenPublisher(JNIEnv *env, jobject jobj, jstring jRtmpUrl)
{
    if (isOpenPub) {
        return;
    }
    LOGI("Java_cn_cloudstep_sayhi_SayHi_OpenPublisher");
    const char* rtmpUrl = (*env)->GetStringUTFChars(env, jRtmpUrl, 0);
    pubRtmpUrl = malloc(strlen(rtmpUrl) + 1);
    memset(pubRtmpUrl, 0, strlen(rtmpUrl) + 1);
    strcpy(pubRtmpUrl, rtmpUrl);
    pthread_create(&openPublisherPid, &attr, openPubliserThread, NULL);
    (*env)->ReleaseStringUTFChars(env, jRtmpUrl, rtmpUrl);
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    ClosePublisher
 * Signature: ()V
 */JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_ClosePublisher(JNIEnv *env, jobject jobj)
{
    pthread_mutex_lock(&recodMutex);
    isStartPub = 0;
    pthread_cond_signal(&recodCond);
    pthread_mutex_unlock(&recodMutex);
    LOGI("Java_cn_cloudstep_sayhi_SayHi_ClosePublisher");
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    OpenPlayer
 * Signature: (Ljava/lang/String{})V
 */

JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_OpenPlayer(JNIEnv *env, jobject jobj, jstring jRtmpUrl)
{
    if (isOpenPlay) {
        return;
    }
    LOGI("Java_cn_cloudstep_sayhi_SayHi_OpenPlayer");
    const char* rtmpUrl = (*env)->GetStringUTFChars(env, jRtmpUrl, 0);
    playRtmpUrl = malloc(strlen(rtmpUrl) + 1);
    strcpy(playRtmpUrl, rtmpUrl);
    pthread_create(&openPlayerPid, &attr, openPlayerThread, NULL);
    (*env)->ReleaseStringUTFChars(env, jRtmpUrl, rtmpUrl);
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    ClosePlayer
 * Signature: ()V
 */

JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_ClosePlayer(JNIEnv *env, jobject jobj)
{
    isStartPlay = 0;
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    Deinit
 * Signature: ()V
 */JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_Deinit(JNIEnv *env, jobject jobj)
{
    (*env)->DeleteGlobalRef(env, gObj);
}
