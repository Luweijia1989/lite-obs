#include "lite-obs/output/aoa_output.h"
#include "lite-obs/lite_obs_platform_config.h"


#include <thread>
#if TARGET_PLATFORM == PLATFORM_ANDROID
#include <jni.h>
#include <jmi.h>
#endif
#include "lite-obs/lite_encoder.h"


#if TARGET_PLATFORM == PLATFORM_ANDROID
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    jmi::javaVM(vm, JNI_VERSION_1_6);
    return JNI_VERSION_1_6;
}
#endif

//#define DUMP_VIDEO

struct aoa_output_private
{
    std::thread stop_thread;
    bool initilized{};
    bool sent_header{};

#if TARGET_PLATFORM == PLATFORM_ANDROID
    jobject phone_camera{};
#endif
    uint8_t header[13]{};
#ifdef DUMP_VIDEO
    FILE *dump_file{};
#endif
};

aoa_output::aoa_output()
{
    d_ptr = std::make_unique<aoa_output_private>();
}

aoa_output::~aoa_output()
{
#if TARGET_PLATFORM == PLATFORM_ANDROID
    if (d_ptr->phone_camera)
        jmi::getEnv()->DeleteGlobalRef(d_ptr->phone_camera);
#endif
}

void aoa_output::i_set_output_info(void *info)
{
#if TARGET_PLATFORM == PLATFORM_ANDROID
    d_ptr->phone_camera = jmi::getEnv()->NewGlobalRef((jobject)info);
#endif
}

bool aoa_output::i_output_valid()
{
    return d_ptr->initilized;
}

bool aoa_output::i_has_video()
{
    return true;
}

bool aoa_output::i_has_audio()
{
    return false;
}

bool aoa_output::i_encoded()
{
    return true;
}

bool aoa_output::i_create()
{
    d_ptr->initilized = true;
    return true;
}

void aoa_output::i_destroy()
{
    if (d_ptr->stop_thread.joinable())
        d_ptr->stop_thread.join();

    d_ptr->initilized = false;
    d_ptr.reset();
}

bool aoa_output::i_start()
{
    if (!lite_obs_output_can_begin_data_capture())
        return false;
    if (!lite_obs_output_initialize_encoders())
        return false;

    if (d_ptr->stop_thread.joinable())
        d_ptr->stop_thread.join();

#ifdef DUMP_VIDEO
    char path[100] = {};
    static int count = 0;
    count++;
    sprintf_s(path, "dump_%d.h264", count);
    d_ptr->dump_file = fopen(path, "wb");
#endif
    lite_obs_output_begin_data_capture();
    d_ptr->sent_header = false;
    return true;
}

void aoa_output::stop_thread(void *data)
{
    auto context = (aoa_output *)data;
    context->lite_obs_output_end_data_capture();
}

void aoa_output::i_stop(uint64_t ts)
{
    if (d_ptr->stop_thread.joinable())
        d_ptr->stop_thread.join();

    d_ptr->stop_thread = std::thread(stop_thread, this);
#ifdef DUMP_VIDEO
    fclose(d_ptr->dump_file);
#endif
}

void aoa_output::i_raw_video(video_data *frame)
{

}

void aoa_output::i_raw_audio(audio_data *frames)
{

}

void aoa_output::i_encoded_packet(std::shared_ptr<encoder_packet> packet)
{
    if (packet->type != obs_encoder_type::OBS_ENCODER_VIDEO)
        return;

    auto send = [this](uint8_t *data, size_t len, int64_t pts){
        memcpy(d_ptr->header, &len, 4);
        d_ptr->header[4] = 1;
        memcpy(d_ptr->header + 5, &pts, 8);

#if TARGET_PLATFORM == PLATFORM_ANDROID
        auto cls = jmi::getEnv()->GetObjectClass(d_ptr->phone_camera);
        auto method = jmi::getEnv()->GetMethodID(cls, "onVideoData", "([B)V");
        auto bytes = jmi::getEnv()->NewByteArray(len + 13);
        jmi::getEnv()->SetByteArrayRegion(bytes, 0, 13, (const jbyte*)d_ptr->header);
        jmi::getEnv()->SetByteArrayRegion(bytes, 13, len, (const jbyte*)data);
        jmi::getEnv()->CallVoidMethod(d_ptr->phone_camera, method, bytes);
        jmi::getEnv()->DeleteLocalRef(bytes);
#endif
    };

    if (!d_ptr->sent_header) {
        d_ptr->sent_header = true;

        auto vencoder = lite_obs_output_get_video_encoder();
        if (!vencoder)
            return;

        uint8_t *header = nullptr;
        size_t size = 0;
        vencoder->lite_obs_encoder_get_extra_data(&header, &size);
#ifdef DUMP_VIDEO
        fwrite(header, 1, size, d_ptr->dump_file);
#endif
        send(header, size, (int64_t)UINT64_C(0x8000000000000000));
    }

#ifdef DUMP_VIDEO
    fwrite(packet->data->data(), 1, packet->data->size(), d_ptr->dump_file);
#endif
    send(packet->data->data(), packet->data->size(), 0);
}

uint64_t aoa_output::i_get_total_bytes()
{
    return 0;
}

int aoa_output::i_get_dropped_frames()
{
    return 0;
}
