package com.example.liteobs_android_example;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import android.Manifest;

import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import com.example.liteobs_android_example.databinding.ActivityMainBinding;
import com.liteobskit.sdk.AOAStreamer;


public class MainActivity extends AppCompatActivity implements Camera2FrameCallback {

    private static String TAG = "MainActivity";
    private static final String[] REQUEST_PERMISSIONS = {
            Manifest.permission.CAMERA,
            Manifest.permission.RECORD_AUDIO
    };

    private static final int CAMERA_PERMISSION_REQUEST_CODE = 1;
    private Camera2Wrapper camera2Wrapper;
//    private MicRecoder micRecoder;

    private ActivityMainBinding binding;
    private AOAStreamer mAOAStreamer;
    TextView mDebugView;

    protected boolean hasPermissionsGranted(String[] permissions) {
        for (String permission : permissions) {
            if (ActivityCompat.checkSelfPermission(this, permission)
                    != PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }
        return true;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        camera2Wrapper = new Camera2Wrapper(this);
        mAOAStreamer = new AOAStreamer(this, new AOAStreamer.UsbConnectCallback() {
            @Override
            public void onConnect() {
                mDebugView.append("usb connected!!!!, can click start stream\n");
            }

            @Override
            public void onDisconnect() {
                mDebugView.append("usb disconnected!!!!, stream stopped, switch ui state\n");
            }

            @Override
            public void onLog(String log) {
                mDebugView.append(log);
                mDebugView.append("\n");
            }
        });

        mAOAStreamer.init();
        mAOAStreamer.initStreamInfo(720, 1280, 20);


        //micRecoder = new MicRecoder(liteOBS.getApiPtr(), this);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        Button startOutput = findViewById(R.id.start_stream);
        Button stopOutput = findViewById(R.id.stop_stream);
        mDebugView = findViewById(R.id.debug_view);
        startOutput.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                //mPhoneCamera.startStream("rtmp://192.168.16.28/live/test");
                mAOAStreamer.startStream();
            }
        });

        stopOutput.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mAOAStreamer.stopStream();
            }
        });

        if (hasPermissionsGranted(REQUEST_PERMISSIONS)) {
            camera2Wrapper.startCamera();
        } else {
            ActivityCompat.requestPermissions(this, REQUEST_PERMISSIONS, CAMERA_PERMISSION_REQUEST_CODE);
        }
    }

    public void onResume() {
        super.onResume();
        mDebugView.append("onResume\n");
        mAOAStreamer.checkOpenAccessory();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();

        mAOAStreamer.closeAccessory();
        mAOAStreamer.destroy();
        mAOAStreamer = null;
    }

    @Override
    public void onPreviewFrame(byte[] data, int width, int height) {
        int[] ls = new int[]{width, width/2, width/2};
        mAOAStreamer.outputVideo(data, ls, width, height);
    }

    @Override
    public void onCaptureFrame(byte[] data, int width, int height) {

    }

    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (requestCode == CAMERA_PERMISSION_REQUEST_CODE) {
            if (hasPermissionsGranted(REQUEST_PERMISSIONS)) {
                camera2Wrapper.startCamera();
            } else {
                Toast.makeText(this, "We need the camera permission.", Toast.LENGTH_SHORT).show();
            }
        } else {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        }
    }
}