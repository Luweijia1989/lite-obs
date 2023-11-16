#include <jni.h>
#include <string>

#include <lite-obs/lite_obs.h>

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_liteobs_1android_1example_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

lite_obs_api *g_obs = nullptr;

extern "C" JNIEXPORT void JNICALL
Java_com_example_liteobs_1android_1example_MainActivity_setupLiteOBS(
        JNIEnv* env,
        jobject /* this */) {
    g_obs = lite_obs_api_new();
}
