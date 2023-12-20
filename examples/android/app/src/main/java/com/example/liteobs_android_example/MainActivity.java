package com.example.liteobs_android_example;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import android.Manifest;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import com.example.liteobs_android_example.databinding.ActivityMainBinding;
import com.liteobskit.sdk.PhoneCamera;


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
    private PhoneCamera mPhoneCamera;
    UsbManager mUsbManager;
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

    private void usbInit() {
        mUsbManager = (UsbManager)getSystemService(Context.USB_SERVICE);
        IntentFilter filter = new IntentFilter(UsbManager.ACTION_USB_ACCESSORY_DETACHED);
        registerReceiver(mUsbReceiver, filter);
    }

    private void usbUninit() {
        unregisterReceiver(mUsbReceiver);
    }

    private final BroadcastReceiver mUsbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (UsbManager.ACTION_USB_ACCESSORY_DETACHED.equals(action)) {
                UsbAccessory accessory = (UsbAccessory) intent.getParcelableExtra(UsbManager.EXTRA_ACCESSORY);
                if (accessory != null) {
                    mPhoneCamera.closeAccessory();
                }
            }
        }
    };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        usbInit();

        camera2Wrapper = new Camera2Wrapper(this);
        mPhoneCamera = new PhoneCamera(this, new PhoneCamera.UsbConnectCallback() {
            @Override
            public void onConnect() {
                mDebugView.append("usb connected!!!!\n");
            }

            @Override
            public void onDisconnect() {
                mDebugView.append("usb disconnected!!!!\n");
            }

            @Override
            public void onLog(String log) {
                mDebugView.append(log);
                mDebugView.append("\n");
            }
        });

        mPhoneCamera.onCreate();

        //micRecoder = new MicRecoder(liteOBS.getApiPtr(), this);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        Button startOutput = findViewById(R.id.start_stream);
        Button stopOutput = findViewById(R.id.stop_stream);
        mDebugView = findViewById(R.id.debug_view);
        startOutput.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mPhoneCamera.startStream("rtmp://192.168.16.28/live/test");
            }
        });

        stopOutput.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mPhoneCamera.stopStream();
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

        mDebugView.append("onResumen");

        UsbAccessory[] accessories = mUsbManager.getAccessoryList();
        UsbAccessory accessory = (accessories == null ? null : accessories[0]);
        if (accessory != null) {
            if (mUsbManager.hasPermission(accessory)) {
                Log.d(TAG, "openAccessory in resume");
                mPhoneCamera.openAccessory(accessory);
            } else {
                Log.d(TAG, "fail to openAccessory, no permission");
            }
        } else {
            Log.d(TAG, "mAccessory is null");
        }
    }
    @Override
    public void onPause() {
        super.onPause();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();

        mPhoneCamera.closeAccessory();
        usbUninit();

        mPhoneCamera.destroy();
        mPhoneCamera = null;
    }

    @Override
    public void onPreviewFrame(byte[] data, int width, int height) {
        int[] ls = new int[]{width, width/2, width/2};
        mPhoneCamera.outputVideo(data, ls, width, height);
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