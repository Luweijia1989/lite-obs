package com.example.liteobs_android_example;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import android.content.Context;
import android.content.Intent;
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

public class MainActivity extends AppCompatActivity {

    private static String TAG = "MainActivity";

    // Used to load the 'liteobs_android_example' library on application startup.
    static {
        System.loadLibrary("liteobs_android_example");
    }

    private ActivityMainBinding binding;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        Button startButton = findViewById(R.id.start_stream);
        startButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startScreenRecording();
            }
        });
    }

    private void startScreenRecording() {
        setupLiteOBS();
    }
    /**
     * A native method that is implemented by the 'liteobs_android_example' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
    public native void setupLiteOBS();
}