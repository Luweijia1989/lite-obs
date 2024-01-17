#include "lite-obs/output/iOS_muxd_output.h"
#include "lite-obs/lite_obs_platform_config.h"


#include <thread>
#include <unistd.h>
#include "lite-obs/lite_encoder.h"

struct iOS_muxd_output_private
{
    std::thread stop_thread;
    bool initilized{};
    bool sent_header{};
    int fd = -1;
    std::vector<uint8_t> buffer;
};

iOS_muxd_output::iOS_muxd_output()
{
    d_ptr = std::make_unique<iOS_muxd_output_private>();
}

iOS_muxd_output::~iOS_muxd_output()
{
}

void iOS_muxd_output::i_set_output_info(void *info)
{
    d_ptr->fd = *(int *)info;
}

bool iOS_muxd_output::i_output_valid()
{
    return d_ptr->initilized;
}

bool iOS_muxd_output::i_has_video()
{
    return true;
}

bool iOS_muxd_output::i_has_audio()
{
    return false;
}

bool iOS_muxd_output::i_encoded()
{
    return true;
}

bool iOS_muxd_output::i_create()
{
    d_ptr->initilized = true;
    return true;
}

void iOS_muxd_output::i_destroy()
{
    if (d_ptr->stop_thread.joinable())
        d_ptr->stop_thread.join();

    d_ptr->initilized = false;
    d_ptr.reset();
}

bool iOS_muxd_output::i_start()
{
    if (!lite_obs_output_can_begin_data_capture())
        return false;
    if (!lite_obs_output_initialize_encoders())
        return false;

    if (d_ptr->stop_thread.joinable())
        d_ptr->stop_thread.join();

    lite_obs_output_begin_data_capture();
    d_ptr->sent_header = false;
    return true;
}

void iOS_muxd_output::stop_thread(void *data)
{
    auto context = (iOS_muxd_output *)data;
    context->lite_obs_output_end_data_capture();
}

void iOS_muxd_output::i_stop(uint64_t ts)
{
    if (d_ptr->stop_thread.joinable())
        d_ptr->stop_thread.join();

    d_ptr->stop_thread = std::thread(stop_thread, this);
#ifdef DUMP_VIDEO
    fclose(d_ptr->dump_file);
#endif
}

void iOS_muxd_output::i_raw_video(video_data *frame)
{

}

void iOS_muxd_output::i_raw_audio(audio_data *frames)
{

}

void iOS_muxd_output::i_encoded_packet(std::shared_ptr<encoder_packet> packet)
{
    if (packet->type != obs_encoder_type::OBS_ENCODER_VIDEO)
        return;

    auto sendMediaData = [this](uint8_t *data, size_t len, int64_t pts){
        if (d_ptr->fd < 0)
            return;

        auto tsSize = sizeof(int64_t);
        auto size = tsSize + len;
        if (d_ptr->buffer.size() < size)
        {
            d_ptr->buffer.resize(size);
        }

        memcpy(d_ptr->buffer.data(), &pts, tsSize);
        memcpy(d_ptr->buffer.data() + tsSize, data, len);
        auto ret = write(d_ptr->fd, d_ptr->buffer.data(), size);
        if (ret < 0) {
            lite_obs_output_signal_stop(LITE_OBS_OUTPUT_DISCONNECTED);
            d_ptr->fd = -1;
        }
    };

    if (!d_ptr->sent_header) {
        d_ptr->sent_header = true;

        auto vencoder = lite_obs_output_get_video_encoder();
        if (!vencoder)
            return;

        uint8_t *header = nullptr;
        size_t size = 0;
        vencoder->lite_obs_encoder_get_extra_data(&header, &size);

        sendMediaData(header, size, (int64_t)UINT64_C(0x8000000000000000));
    }

    sendMediaData(packet->data->data(), packet->data->size(), 0);
}

uint64_t iOS_muxd_output::i_get_total_bytes()
{
    return 0;
}

int iOS_muxd_output::i_get_dropped_frames()
{
    return 0;
}
