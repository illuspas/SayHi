SayHi
=====

Native audio recording/playing and speex encoding/decoding,transport by rtmp.

 1. 采集播放,使用ANDROID_PLATFORM_9开始支持的OpenSL ES接口本地代码实现
 2. 编解码,使用libspeex_1.2rc1,开启ARM5E汇编优化,使用fixed-point编译
 3. 传输,使用librtmp git 版本

工作目录下libs\armeabi\libsayhi.so并非最新版本,使用时请用ndk-build重新生成  

最近经常有同学邮件反馈这个项目的问题，抱歉，几年前的项目了，坑多，懒得填了，仅供参考。 如果做商用，可以试试这个商用版https://github.com/NodeMedia/NodeMediaClient-Android
