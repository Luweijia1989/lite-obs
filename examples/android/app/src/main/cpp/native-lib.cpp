#include <jni.h>
#include <string>
#include <android/log.h>

#include <lite-obs/lite_obs.h>

#define TAG "lite-obs-example"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,TAG ,__VA_ARGS__)

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_liteobs_1android_1example_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

lite_obs_api *g_obs = nullptr;

static void log_handler(int log_level, const char *msg)
{
    LOGD("lite-obs log===> %s", msg);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_liteobs_1android_1example_MainActivity_setupLiteOBS(
        JNIEnv* env,
        jobject /* this */) {
    lite_obs_set_log_handle(log_handler);
    g_obs = lite_obs_api_new();
    int ret = g_obs->lite_obs_reset_video(g_obs, 720, 1280, 20);
    LOGD("lite_obs_reset_video ret: %d", ret);
    bool r = g_obs->lite_obs_reset_audio(g_obs, 48000);
    LOGD("lite_obs_reset_audio ret: %d", r);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_liteobs_1android_1example_MainActivity_startOutput(JNIEnv *env, jobject thiz) {
    lite_obs_output_callbak cb{};
    cb.start = [](void *){LOGD("===start");};
    cb.stop = [](int code, const char *msg, void *){LOGD("===stop");};
    cb.starting = [](void *){LOGD("===starting");};
    cb.stopping = [](void *){LOGD("===stopping");};
    cb.activate = [](void *){LOGD("===activate");};
    cb.deactivate = [](void *){LOGD("===deactivate");};
    cb.reconnect = [](void *){LOGD("===reconnect");};
    cb.reconnect_success = [](void *){LOGD("===reconnect_success");};
    cb.connected = [](void *){LOGD("===connected");};
    cb.first_media_packet = [](void *){LOGD("===first_media_packet");};
    cb.opaque = nullptr;
    g_obs->lite_obs_start_output(g_obs, "rtmp://192.168.16.28/live/test", 4000, 160, cb);
}