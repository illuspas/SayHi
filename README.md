SayHi
=====

Native audio recording/playing and speex encoding/decoding,transport by rtmp.

 1. 采集播放,使用ANDROID_PLATFORM_9开始支持的OpenSL ES接口本地代码实现
 2. 编解码,使用libspeex_1.2rc1,关闭vbr,开启ARM5E汇编优化,使用fixed-point编译
 3. 传输,使用librtmp git 版本,删除SendCheckBW处理代码

工作目录下libs\armeabi\libsayhi.so并非最新版本,使用时请用ndk-build重新生成