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

JavaVM *gJvm = NULL;
jobject gObj = NULL;
jmethodID eventMid;
pthread_attr_t attr;

//publisher variable
char* pubRtmpUrl;
pthread_t openPublisherPid;
pthread_mutex_t recodMutex;
pthread_cond_t recodCond;
int isStartPub;

SpeexBits ebits;     //speex
int enc_frame_size;
void *enc_state;



//player variable
int dec_frame_size; //speex
SpeexBits dbits;
void *dec_state;

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
    int i,enc_size;
    short* pcm_buffer;
    char*  output_buffer;
    int compression = 4;
    (*gJvm)->AttachCurrentThread(gJvm, &env, NULL);
    pthread_mutex_init(&recodMutex, NULL);
    pthread_cond_init(&recodCond, NULL);
    (*env)->CallVoidMethod(env, gObj, eventMid, 1); //Start init native audio recoder
    if(!initNativeRecoder()) {
        (*env)->CallVoidMethod(env, gObj, eventMid, 2);
        return NULL;
    }
    //init speex encoder
    speex_bits_init(&ebits);
    enc_state = speex_encoder_init(&speex_nb_mode);
    speex_encoder_ctl(enc_state, SPEEX_SET_QUALITY, &compression);
    speex_encoder_ctl(enc_state, SPEEX_GET_FRAME_SIZE, &enc_frame_size);
    pcm_buffer = malloc(enc_frame_size*sizeof(short));
    output_buffer = malloc(enc_frame_size*sizeof(char));
    LOGI("Speex Encoder init,enc_frame_size:%d\n",enc_frame_size);

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
        gettimeofday(&start, NULL);
        for(i=0;i<RECORDER_FRAMES;i+=enc_frame_size)
        {
//            LOGI("encode position:%d\n",i);
            speex_bits_reset(&ebits);
            memcpy(pcm_buffer,recorderBuffer+i,enc_frame_size);
            speex_encode_int(enc_state, pcm_buffer, &ebits);
            enc_size = speex_bits_write(&ebits,output_buffer,enc_frame_size);

        }
        gettimeofday(&end, NULL);
        int timeuse = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
        LOGI("Encoding finish timeuse:%d\n",timeuse);
    }
    result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_STOPPED);
    if (SL_RESULT_SUCCESS == result) {
        LOGI("SetRecordState SL_RECORDSTATE_STOPPED!\n");
    }
    (*env)->CallVoidMethod(env, gObj, eventMid, 4);
    (*gJvm)->DetachCurrentThread(gJvm);
    free(pcm_buffer);
    free(output_buffer);
    free(pubRtmpUrl);
    speex_bits_destroy(&ebits);
    speex_encoder_destroy(enc_state);
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    Init
 * Signature: ()V
 */JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_Init(JNIEnv *env, jobject jobj)
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
    LOGI("Java_cn_cloudstep_sayhi_SayHi_OpenPublisher");
    const char* rtmpUrl = (*env)->GetStringUTFChars(env, jRtmpUrl, 0);
    pubRtmpUrl = malloc(strlen(rtmpUrl) + 1);
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
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    ClosePlayer
 * Signature: ()V
 */

JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_ClosePlayer(JNIEnv *env, jobject jobj)
{
}

/*
 * Class:     cn_cloudstep_sayhi_SayHi
 * Method:    Deinit
 * Signature: ()V
 */JNIEXPORT void JNICALL Java_cn_cloudstep_sayhi_SayHi_Deinit(JNIEnv *env, jobject jobj)
{
    (*env)->DeleteGlobalRef(env, gObj);
}
