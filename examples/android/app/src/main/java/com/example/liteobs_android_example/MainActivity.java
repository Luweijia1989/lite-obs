package com.example.liteobs_android_example;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import android.Manifest;

import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;

import com.liteobskit.sdk.LiteOBS;
import com.liteobskit.sdk.LiteOBSSource;

import com.example.liteobs_android_example.databinding.ActivityMainBinding;
import com.liteobskit.sdk.PhoneCameraActivity;

import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;

public class MainActivity extends PhoneCameraActivity implements Camera2FrameCallback {

    private static String TAG = "MainActivity";
    private static final String[] REQUEST_PERMISSIONS = {
            Manifest.permission.CAMERA,
            Manifest.permission.RECORD_AUDIO
    };

    private static final int CAMERA_PERMISSION_REQUEST_CODE = 1;
    private Camera2Wrapper camera2Wrapper;
    private MicRecoder micRecoder;
    private LiteOBSSource videoSource;

    private ActivityMainBinding binding;
    private FileOutputStream fileStream;

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

        videoSource = new LiteOBSSource(liteOBS.getApiPtr(), 5);
        videoSource.rotate(-90.f);
        micRecoder = new MicRecoder(liteOBS.getApiPtr(), this);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        Button startOutput = findViewById(R.id.start_stream);
        startOutput.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                micRecoder.startRecord();

//                liteOBS.test(fileStream, liteOBS);
                liteOBS.startStream("rtmp://192.168.16.28/live/test");
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
    }

    @Override
    public void onPreviewFrame(byte[] data, int width, int height) {
        int[] ls = new int[]{width, width/2, width/2};
        videoSource.outputVideo(data, ls, width, height);
    }

    @Override
    public void onCaptureFrame(byte[] data, int width, int height) {

    }

    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (requestCode == CAMERA_PERMISSION_REQUEST_CODE) {
            if (hasPermissionsGranted(REQUEST_PERMISSIONS)) {
//                mCamera2Wrapper.startCamera();
            } else {
                //Toast.makeText(this, "We need the camera permission.", Toast.LENGTH_SHORT).show();
            }
        } else {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        }
    }
}