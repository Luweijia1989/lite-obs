#pragma once

#include <memory>
#include "video_info.h"
#include "video_frame.h"
#include "video_scaler.h"

#define VIDEO_OUTPUT_SUCCESS 0
#define VIDEO_OUTPUT_INVALIDPARAM -1
#define VIDEO_OUTPUT_FAIL -2

struct video_data {
    video_frame frame;
    uint64_t timestamp{};
};

struct video_output_info {
    const char *name{};

    video_format format = video_format::VIDEO_FORMAT_NONE;
    uint32_t fps_num{};
    uint32_t fps_den{};
    uint32_t width{};
    uint32_t height{};
    size_t cache_size{};

    video_colorspace colorspace = video_colorspace::VIDEO_CS_DEFAULT;
    video_range_type range = video_range_type::VIDEO_RANGE_DEFAULT;
};

struct video_output_private;
struct video_input;
class video_output
{
public:
    video_output();
    ~video_output();

    static void video_thread(void *arg);

    int video_output_open(video_output_info *info);
    void video_output_close();

    uint32_t video_output_get_width();
    uint32_t video_output_get_height();

    video_output_info *video_output_get_info();

    bool video_output_connect(const video_scale_info *conversion, void (*callback)(void *param, struct video_data *frame), void *param);
    void video_output_disconnect(void (*callback)(void *param, video_data *frame), void *param);

    void video_output_stop();
    bool video_output_stopped();

    bool video_output_lock_frame(video_frame *frame, int count, uint64_t timestamp);
    void video_output_unlock_frame();

    uint64_t video_output_get_frame_time();
    uint32_t video_output_get_total_frames();

private:
    void video_thread_internal();
    bool video_output_cur_frame();

    void init_cache();
    int video_get_input_idx(void (*callback)(void *param, video_data *frame), void *param);
    bool video_input_init(std::shared_ptr<video_input> input);
    void reset_frames();
    void log_skipped();
    bool scale_video_output(std::shared_ptr<video_input> input, video_data *data);

private:
    std::unique_ptr<video_output_private> d_ptr{};
};
