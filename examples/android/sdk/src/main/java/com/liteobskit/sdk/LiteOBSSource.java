package com.liteobskit.sdk;
public class LiteOBSSource {
    private long sourcePtr;
    private long obsPtr;

    public LiteOBSSource(long obsPtr, int type) {
        this.obsPtr = obsPtr;
        sourcePtr = createSource(obsPtr, type);
    }
    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        deleteSource(obsPtr, sourcePtr);
    }

    public void outputVideo(byte[] data, int[] linesize, int width, int height) {
        outputVideo(sourcePtr, data, linesize, width, height);
    }

    public void rotate(float rot) {
        rotate(sourcePtr, rot);
    }

    private native long createSource(long obsPtr, int type);
    private native void deleteSource(long obsPtr, long sourcePtr);
    private native void outputVideo(long sourcePtr, byte[] data, int[] linesize, int width, int height);
    private native void rotate(long sourcePtr, float rot);
}
