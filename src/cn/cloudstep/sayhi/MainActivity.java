package cn.cloudstep.sayhi;

import cn.cloudstep.sayhi.SayHi.OnEventCallback;
import android.app.Activity;
import android.os.Bundle;
import android.view.Menu;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;

public class MainActivity extends Activity {
	SayHi say;
	Button bt1, bt2;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);
		say = new SayHi();
		say.Init();
		say.setOnEventCallback(new OnEventCallback() {
			
			@Override
			public void onEvent(int event) {
				if(event == 3)
				{
					say.OpenPlayer("rtmp://192.168.0.10/live/main1 live=1");
				}
			}
		});
		bt1 = (Button) findViewById(R.id.button1);
		bt2 = (Button) findViewById(R.id.button2);
		bt1.setOnClickListener(new OnClickListener() {

			@Override
			public void onClick(View v) {
				say.OpenPublisher("rtmp://192.168.0.10/live/main1");
			}
		});
		bt2.setOnClickListener(new OnClickListener() {

			@Override
			public void onClick(View v) {
				say.ClosePublisher();
				say.ClosePlayer();
			}
		});
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		// Inflate the menu; this adds items to the action bar if it is present.
		getMenuInflater().inflate(R.menu.activity_main, menu);
		return true;
	}

	@Override
	protected void onDestroy() {
		super.onDestroy();

	}
}
