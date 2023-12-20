package com.liteobskit.sdk;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbManager;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

public class PhoneCamera {
    private static final String ACTION_USB_PERMISSION = "com.liteobskit.sdk.action.USB_PERMISSION";
    private static final String TAG = "PhoneCameraActivity==> ";
    public LiteOBS liteOBS;
    private UsbManager mUsbManager;
    private UsbAccessory mAccessory;
    private ParcelFileDescriptor mFileDescriptor;
    private FileInputStream mInputStream;
    private FileOutputStream mOutputStream;
    private Context mCtx;
    private LiteOBSSource mVideoSource;

    private boolean mConnected = false;
    private UsbConnectCallback mUsbCallback;

    public interface UsbConnectCallback {
        void onConnect();
        void onDisconnect();
        void onLog(String log);
    }

    public PhoneCamera(Context ctx, UsbConnectCallback cb) {
        mCtx = ctx;
        mUsbCallback = cb;
        mUsbManager = (UsbManager)ctx.getSystemService(Context.USB_SERVICE);
    }

    public void onCreate() {

        liteOBS = new LiteOBS();
        liteOBS.resetVideoAudio(720, 1280, 20);

        mVideoSource = new LiteOBSSource(liteOBS.getApiPtr(), 5);
        mVideoSource.rotate(-90.f);
    }

    public void destroy() {
        mVideoSource = null;
        liteOBS = null;
    }

    public UsbAccessory getUsbAccessory() {
        return mAccessory;
    }

    public void openAccessory(UsbAccessory accessory) {
        if (mConnected) {
            Log.d(TAG, "openAccessory: already opened!");
            return;
        }
        mFileDescriptor = mUsbManager.openAccessory(accessory);
        if (mFileDescriptor != null) {
            mAccessory = accessory;
            FileDescriptor fd = mFileDescriptor.getFileDescriptor();
            mInputStream = new FileInputStream(fd);
            mOutputStream = new FileOutputStream(fd);
            mConnected = true;
            mUsbCallback.onConnect();
            Log.d(TAG, "accessory opened");
        } else {
            Log.d(TAG, "accessory open fail");
        }
    }

    public void closeAccessory() {
        if (!mConnected) {
            Log.d(TAG, "closeAccessory: not connected");
            return;
        }

        try {
            if (mFileDescriptor != null)
                mFileDescriptor.close();
            if (mInputStream != null)
                mInputStream.close();
            if (mOutputStream != null)
                mOutputStream.close();
        } catch (IOException e) {
        } finally {
            mFileDescriptor = null;
            mInputStream = null;
            mOutputStream = null;
            mAccessory = null;
        }

        mConnected = false;
        liteOBS.stopStream();
        mUsbCallback.onDisconnect();
    }

    public void outputVideo(byte[] data, int[] linesize, int width, int height) {
        mVideoSource.outputVideo(data, linesize, width, height);
    }

    public void startStream() {
        if (mConnected && mOutputStream != null) {
            mUsbCallback.onLog("request start stream");
            liteOBS.startStream(mOutputStream);
        }
    }
}
