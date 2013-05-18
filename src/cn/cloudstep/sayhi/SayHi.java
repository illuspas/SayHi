package cn.cloudstep.sayhi;

public class SayHi {
	public native void Init();
	public native void OpenPublisher(String rtmpUrl);
	public native void ClosePublisher();
	public native void OpenPlayer(String rtmpUrl);
	public native void ClosePlayer();
	public native void Deinit();
	
	private void onEventCallback(int event) {
		System.out.println("onEventCallback: "+ event);
	}
	static {
		System.loadLibrary("sayhi");
	}
}
