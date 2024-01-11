package com.liteobskit.sdk;

import android.app.Activity;
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
import java.util.Timer;
import java.util.TimerTask;

public class AOAStreamer {
    private static final String TAG = "PhoneCamera==> ";
    public LiteOBS liteOBS;
    private LiteOBSSource mVideoSource;
    private UsbManager mUsbManager;
    private UsbAccessory mAccessory;
    private ParcelFileDescriptor mFileDescriptor;
    private FileInputStream mInputStream;
    private FileOutputStream mOutputStream;
    private Thread mReadThread;
    private long mLastHeartBeatTime;
    private Timer mHeartBeatTimer;
    private Activity mCtx;

    private boolean mConnected = false;
    private UsbConnectCallback mUsbCallback;

    public interface UsbConnectCallback {
        void onConnect();
        void onDisconnect();
        void onLog(String log);
    }

    public AOAStreamer(Activity ctx, UsbConnectCallback cb) {
        mCtx = ctx;
        mUsbCallback = cb;
    }

    public void init() {
        liteOBS = new LiteOBS();
        mVideoSource = new LiteOBSSource(liteOBS.getApiPtr(), 5);
        mVideoSource.rotate(-90.f);
        mUsbManager = (UsbManager)mCtx.getSystemService(Context.USB_SERVICE);
    }

    public void destroy() {
        mVideoSource = null;
        liteOBS = null;
    }

    public void checkOpenAccessory() {
        UsbAccessory[] accessories = mUsbManager.getAccessoryList();
        UsbAccessory accessory = (accessories == null ? null : accessories[0]);
        if (accessory != null) {
            if (mUsbManager.hasPermission(accessory)) {
                doLog("openAccessory in resume");
                openAccessory(accessory);
            } else {
                doLog("fail to openAccessory, no permission");
            }
        } else {
            doLog("mAccessory is null");
        }
    }

    private void openAccessory(UsbAccessory accessory) {
        if (mConnected) {
            doLog("openAccessory: already opened!");
            return;
        }
        mFileDescriptor = mUsbManager.openAccessory(accessory);
        if (mFileDescriptor != null) {
            mAccessory = accessory;
            FileDescriptor fd = mFileDescriptor.getFileDescriptor();
            mInputStream = new FileInputStream(fd);
            mOutputStream = new FileOutputStream(fd);
            readInternal();
            mConnected = true;
            doLog("accessory opened, start read from input stream");
        } else {
            doLog("accessory open fail");
        }
    }

    public void closeAccessory() {
        if (!mConnected) {
            doLog("closeAccessory: not connected");
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

        stopHeartBeatTimer();

        try {
            mReadThread.join();
        } catch (Exception e) {
            e.printStackTrace();
        }

        mConnected = false;
    }

    private void startHeartBeatTimer() {
        if (mHeartBeatTimer != null)
            return;

        mLastHeartBeatTime = System.currentTimeMillis();
        mHeartBeatTimer = new Timer();
        mHeartBeatTimer.scheduleAtFixedRate(new TimerTask() {
            @Override
            public void run() {
                if (System.currentTimeMillis() - mLastHeartBeatTime > 500) {
                    // timeout occur
                    mCtx.runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            stopHeartBeatTimer();
                            stopStream();
                            closeAccessory();
                            mUsbCallback.onDisconnect();
                        }
                    });
                }
            }
        },0,200);
    }

    public void stopHeartBeatTimer() {
        if (mHeartBeatTimer != null) {
            mHeartBeatTimer.cancel();
            mHeartBeatTimer = null;
        }
    }

    private void doLog(String log) {
        mCtx.runOnUiThread(new Runnable() {

            @Override
            public void run() {
                mUsbCallback.onLog(log);
            }
        });
    }

    private void readInternal() {
        mReadThread = new Thread(new Runnable() {
            @Override
            public void run() {
                doLog("readInternal: start usb read thread");
                while (true) {
                    try {
                        byte[] buf = new byte[1];
                        int len = mInputStream.read(buf);
                        if (len <= 0) {
                            doLog("readInternal: should never happen");
                            break;
                        }

                        if (buf[0] == 2) {
                            mLastHeartBeatTime = System.currentTimeMillis();
                        } else if (buf[0] == 1) {
                            mCtx.runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    startHeartBeatTimer();
                                    mUsbCallback.onConnect();
                                }
                            });
                        }
                    } catch (IOException e) {
                        doLog("readInternal exception: " + e.toString());
                        break;
                    }
                }
                doLog("readInternal: end");
            }
        });
        mReadThread.start();
    }

    public void initStreamInfo(int width, int height, int fps) {
        liteOBS.resetVideoAudio(width, height, fps);
    }

    public void outputVideo(byte[] data, int[] linesize, int width, int height) {
        mVideoSource.outputVideo(data, linesize, width, height);
    }

    public void startStream() {
        if (!mConnected) {
            return;
        }

        liteOBS.startStream(this);
        doLog("startStream: AOA stream start");
    }

    public void stopStream() {
        liteOBS.stopStream();
    }

    public void onVideoData(byte[] data) {
        try {
            mOutputStream.write(data);
            mOutputStream.flush();
        } catch (Exception e) {
            // connection lost
        }
    }
}
