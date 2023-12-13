package com.example.liteobs_android_example;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import android.Manifest;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.opengl.GLES10;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import com.example.liteobs_android_example.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity implements Camera2FrameCallback {

    private static String TAG = "MainActivity";
    private static final String[] REQUEST_PERMISSIONS = {
            Manifest.permission.CAMERA
    };

    private static final int CAMERA_PERMISSION_REQUEST_CODE = 1;
    private Camera2Wrapper camera2Wrapper;
    private LiteOBS liteOBS;
    private LiteOBSSource videoSource;

    private ActivityMainBinding binding;

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
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        camera2Wrapper = new Camera2Wrapper(this);
        liteOBS = new LiteOBS();
        liteOBS.resetVideoAudio(720, 1280, 20);
        videoSource = new LiteOBSSource(liteOBS.getApiPtr(), 5);
        videoSource.rotate(-90.f);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        Button startOutput = findViewById(R.id.start_stream);
        startOutput.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                liteOBS.startStream();
            }
        });

        if (hasPermissionsGranted(REQUEST_PERMISSIONS)) {
            camera2Wrapper.startCamera();
        } else {
            ActivityCompat.requestPermissions(this, REQUEST_PERMISSIONS, CAMERA_PERMISSION_REQUEST_CODE);
        }
    }

    protected void onResume() {
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
                //mCamera2Wrapper.startCamera();
            } else {
                //Toast.makeText(this, "We need the camera permission.", Toast.LENGTH_SHORT).show();
            }
        } else {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        }
    }
}