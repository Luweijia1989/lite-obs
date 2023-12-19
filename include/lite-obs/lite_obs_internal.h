#ifndef LITE_OBS_H
#define LITE_OBS_H

#include <memory>
#include <string>
#include "media-io/video_info.h"
#include "media-io/audio_info.h"
#include "lite-obs/lite_obs_callback.h"

struct lite_obs_private;
class lite_obs_media_source_internal;
class lite_obs_internal
{
public:
    lite_obs_internal();
    ~lite_obs_internal();

    int obs_reset_video(uint32_t width, uint32_t height, uint32_t fps);
    bool obs_reset_audio(uint32_t sample_rate);

    lite_obs_media_source_internal *lite_obs_create_source(source_type type);
    void lite_obs_destroy_source(lite_obs_media_source_internal *source);

    bool lite_obs_start_output(output_type type, void *output_info, int vb, int ab, const lite_obs_output_callbak &callback);
    void lite_obs_stop_output();

    void lite_obs_reset_encoder(bool sw);

private:
    lite_obs_private* d_ptr{};
};

struct lite_obs_media_source_private;
class lite_obs_media_source_internal
{
    friend class lite_obs_internal;
public:

    lite_obs_media_source_internal();
    void output_audio(const uint8_t *audio_data[MAX_AV_PLANES], uint32_t frames, audio_format format, speaker_layout layout, uint32_t sample_rate);
    void output_video(int texId, uint32_t width, uint32_t height);
    void output_video(const uint8_t *video_data[MAX_AV_PLANES], const int line_size[MAX_AV_PLANES],
                      video_format format, video_range_type range,
                      video_colorspace color_space, uint32_t width,
                      uint32_t height);
    void output_video(const uint8_t *img_data, uint32_t img_width, uint32_t img_height);
    void clear_video();

    void set_pos(float x, float y);
    void set_scale(float s_w, float s_h);
    void set_rotate(float rot);
    void set_render_box(int x, int y, int width, int height, source_aspect_ratio_mode mode);
    void set_order(order_movement movement);
    void set_flip(bool flip_h, bool flip_v);
    void reset_transform();

private:
    ~lite_obs_media_source_internal();

private:
    lite_obs_media_source_private *d_ptr{};
};

#endif // LITE_OBS_H
