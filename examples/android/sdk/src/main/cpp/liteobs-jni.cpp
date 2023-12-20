#include <jni.h>
#include <string>
#include <android/log.h>

#include <lite-obs/lite_obs.h>

#define TAG "lite-obs-example"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,TAG ,__VA_ARGS__)

static void log_handler(int log_level, const char *msg)
{
    LOGD("lite-obs log===> %s", msg);
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_liteobskit_sdk_LiteOBS_createLiteOBS(JNIEnv *env, jobject thiz) {
    lite_obs_set_log_handle(log_handler);
    return reinterpret_cast<jlong>(lite_obs_api_new());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBS_deleteLiteOBS(JNIEnv *env, jobject thiz,
                                                                 jlong ptr) {
    lite_obs_api *api_ptr = reinterpret_cast<lite_obs_api *>(ptr);
    lite_obs_api_delete(&api_ptr);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBS_resetVideoAudio(JNIEnv *env, jobject thiz,
                                                                   jlong ptr, jint width,
                                                                   jint height, jint fps) {
    lite_obs_api *api_ptr = reinterpret_cast<lite_obs_api *>(ptr);
    api_ptr->lite_obs_reset_video(api_ptr, width, height, fps);
    api_ptr->lite_obs_reset_audio(api_ptr, 48000);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBS_startAOAStream(JNIEnv *env, jobject thiz, jobject obj, jlong ptr) {
    lite_obs_api *api_ptr = reinterpret_cast<lite_obs_api *>(ptr);
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
    api_ptr->lite_obs_start_output(api_ptr, output_type::android_aoa, obj, 4000, 160, cb);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBS_startRtmpStream(JNIEnv *env, jobject thiz, jstring url, jlong ptr) {
    lite_obs_api *api_ptr = reinterpret_cast<lite_obs_api *>(ptr);
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

    auto c_url = env->GetStringUTFChars(url, nullptr);
    api_ptr->lite_obs_start_output(api_ptr, output_type::rtmp, (void *)c_url, 4000, 160, cb);
    env->ReleaseStringUTFChars(url, c_url);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBS_stopStream(JNIEnv *env, jobject thiz,
                                                              jlong ptr) {
    lite_obs_api *api_ptr = reinterpret_cast<lite_obs_api *>(ptr);
    api_ptr->lite_obs_stop_output(api_ptr);
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_liteobskit_sdk_LiteOBSSource_createSource(JNIEnv *env, jobject thiz,
                                                                      jlong ptr, jint type) {
    lite_obs_api *api_ptr = reinterpret_cast<lite_obs_api *>(ptr);
    return reinterpret_cast<jlong>(lite_obs_media_source_new(api_ptr, (source_type)type));
}
extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBSSource_deleteSource(JNIEnv *env, jobject thiz,
                                                                      jlong obs_ptr,
                                                                      jlong source_ptr) {
    lite_obs_api *api_ptr = reinterpret_cast<lite_obs_api *>(obs_ptr);
    lite_obs_media_source_api *source = reinterpret_cast<lite_obs_media_source_api *>(source_ptr);
    lite_obs_media_source_delete(api_ptr, &source);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBSSource_outputVideo(JNIEnv *env, jobject thiz,
                                                                     jlong source_ptr,
                                                                     jbyteArray data,
                                                                     jintArray linesize, jint width,
                                                                     jint height) {
    jbyte* data_ptr = env->GetByteArrayElements(data, NULL);
    jint* linesize_ptr = env->GetIntArrayElements(linesize, NULL);

    const uint8_t *v_data[MAX_AV_PLANES] = {};
    v_data[0] = (uint8_t *)data_ptr;
    v_data[1] = v_data[0] + width * height;
    v_data[2] = v_data[0] + width * height * 5 / 4;

    lite_obs_media_source_api *source = reinterpret_cast<lite_obs_media_source_api *>(source_ptr);
    source->output_video2(source, v_data, linesize_ptr, VIDEO_FORMAT_I420, VIDEO_RANGE_FULL, VIDEO_CS_709, width, height);

    env->ReleaseIntArrayElements(linesize, linesize_ptr, JNI_ABORT);
    env->ReleaseByteArrayElements(data, data_ptr, JNI_ABORT);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBSSource_rotate(JNIEnv *env, jobject thiz,
                                                                jlong source_ptr, jfloat rot) {
    lite_obs_media_source_api *source = reinterpret_cast<lite_obs_media_source_api *>(source_ptr);
    source->set_rotate(source, rot);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_liteobskit_sdk_LiteOBSSource_outputAudio(JNIEnv *env, jobject thiz, jlong source_ptr,
                                                  jbyteArray data, jint frames) {
    jbyte* data_ptr = env->GetByteArrayElements(data, NULL);

    const uint8_t *a_data[MAX_AV_PLANES] = {};
    a_data[0] = (uint8_t *)data_ptr;

    lite_obs_media_source_api *source = reinterpret_cast<lite_obs_media_source_api *>(source_ptr);
    source->output_audio(source, a_data, frames, audio_format::AUDIO_FORMAT_16BIT, speaker_layout::SPEAKERS_MONO, 44100);

    env->ReleaseByteArrayElements(data, data_ptr, JNI_ABORT);
}