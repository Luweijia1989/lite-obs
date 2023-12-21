package com.liteobskit.sdk;

public class LiteOBS {
    static {
        System.loadLibrary("liteobs_android");
    }
    private long apiPtr;

    public LiteOBS() {
        apiPtr = createLiteOBS();
    }

    public long getApiPtr() {
        return apiPtr;
    }
    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        deleteLiteOBS(apiPtr);
    }
    public void resetVideoAudio(int width, int height, int fps) {
        resetVideoAudio(apiPtr, width, height, fps);
    }
    public void startStream(AOAStreamer output) {
        startAOAStream(output, apiPtr);
    }
    public void startStream(String url) {
        startRtmpStream(url, apiPtr);
    }
    public void stopStream() {
        stopStream(apiPtr);
    }

    private native long createLiteOBS();
    private native void deleteLiteOBS(long ptr);
    private native void resetVideoAudio(long ptr, int width, int height, int fps);
    private native void startAOAStream(AOAStreamer obj, long ptr);
    private native void startRtmpStream(String url, long ptr);
    private native void stopStream(long ptr);
}
