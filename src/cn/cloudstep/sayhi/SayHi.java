package cn.cloudstep.sayhi;

public class SayHi {
	public native void Init();
	public native void OpenPublisher(String rtmpUrl);
	public native void ClosePublisher();
	public native void OpenPlayer(String rtmpUrl);
	public native void ClosePlayer();
	public native void Deinit();
	
	static {
		System.loadLibrary("sayhi");
	}
}
