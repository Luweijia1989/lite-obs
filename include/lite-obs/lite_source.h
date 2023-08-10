#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <map>
#include <list>
#include "lite_obs.h"
#include "media-io/video_frame.h"
#include "media-io/video_info.h"

class gs_texture_render;
class gs_texture;
class lite_obs_core_video;
class lite_obs_core_audio;
struct lite_source_private;
class lite_source : public std::enable_shared_from_this<lite_source>
{
    friend class lite_obs_core_audio;
    friend class lite_obs_core_video;
public:
    struct lite_obs_source_audio_frame {
        const uint8_t *data[MAX_AV_PLANES];
        uint32_t frames{};

        speaker_layout speakers{};
        audio_format format{};
        uint32_t samples_per_sec{};

        uint64_t timestamp{};
    };

    struct lite_obs_source_video_frame {
        std::vector<uint8_t *> data{};
        std::vector<uint32_t> linesize{};
        std::vector<uint8_t> data_internal{};
        uint32_t width{};
        uint32_t height{};
        uint64_t timestamp{};

        video_format format{};
        float color_matrix[16]{};
        bool full_range{};
        float color_range_min[3]{};
        float color_range_max[3]{};
        bool flip{};
        bool flip_h{};

        lite_obs_source_video_frame() {
            data.resize(MAX_AV_PLANES);
            linesize.resize(MAX_AV_PLANES);
        }

        void lite_obs_source_video_frame_init(video_format f, uint32_t w, uint32_t h) {
            video_frame_init(data_internal, data, linesize, f, w, h);
            format = f;
            width = w;
            height = h;
        }

        inline void copy_frame_data_line(const lite_obs_source_video_frame *src, uint32_t plane, uint32_t y) {
            uint32_t pos_src = y * src->linesize[plane];
            uint32_t pos_dst = y * linesize[plane];
            uint32_t bytes = linesize[plane] < src->linesize[plane] ? linesize[plane] : src->linesize[plane];

            memcpy(data[plane] + pos_dst, src->data[plane] + pos_src, bytes);
        }

        inline void copy_frame_data_plane(const lite_obs_source_video_frame *src, uint32_t plane, uint32_t lines)
        {
            if (linesize[plane] != src->linesize[plane])
                for (uint32_t y = 0; y < lines; y++)
                    copy_frame_data_line(src, plane, y);
            else
                memcpy(data[plane], src->data[plane],
                       linesize[plane] * lines);
        }

        void lite_obs_source_video_frame_copy(const lite_obs_source_video_frame *src) {
            flip = src->flip;
            flip_h = src->flip_h;
            full_range = src->full_range;
            timestamp = src->timestamp;
            memcpy(color_matrix, src->color_matrix, sizeof(float) * 16);
            if (!full_range) {
                size_t const size = sizeof(float) * 3;
                memcpy(color_range_min, src->color_range_min, size);
                memcpy(color_range_max, src->color_range_max, size);
            }

            switch (src->format) {
            case video_format::VIDEO_FORMAT_I420:
                copy_frame_data_plane(src, 0, height);
                copy_frame_data_plane(src, 1, height / 2);
                copy_frame_data_plane(src, 2, height / 2);
                break;

            case video_format::VIDEO_FORMAT_NV12:
                copy_frame_data_plane(src, 0, height);
                copy_frame_data_plane(src, 1, height / 2);
                break;

            case video_format::VIDEO_FORMAT_I444:
            case video_format::VIDEO_FORMAT_I422:
                copy_frame_data_plane(src, 0, height);
                copy_frame_data_plane(src, 1, height);
                copy_frame_data_plane(src, 2, height);
                break;

            case video_format::VIDEO_FORMAT_YVYU:
            case video_format::VIDEO_FORMAT_YUY2:
            case video_format::VIDEO_FORMAT_UYVY:
            case video_format::VIDEO_FORMAT_NONE:
            case video_format::VIDEO_FORMAT_RGBA:
            case video_format::VIDEO_FORMAT_BGRA:
            case video_format::VIDEO_FORMAT_BGRX:
            case video_format::VIDEO_FORMAT_Y800:
            case video_format::VIDEO_FORMAT_BGR3:
            case video_format::VIDEO_FORMAT_AYUV:
                copy_frame_data_plane(src, 0, height);
                break;

            case video_format::VIDEO_FORMAT_I40A:
                copy_frame_data_plane(src, 0, height);
                copy_frame_data_plane(src, 1, height / 2);
                copy_frame_data_plane(src, 2, height / 2);
                copy_frame_data_plane(src, 3, height);
                break;

            case video_format::VIDEO_FORMAT_I42A:
            case video_format::VIDEO_FORMAT_YUVA:
                copy_frame_data_plane(src, 0, height);
                copy_frame_data_plane(src, 1, height);
                copy_frame_data_plane(src, 2, height);
                copy_frame_data_plane(src, 3, height);
                break;
            }
        }
    };

    struct lite_obs_source_video_frame2 {
        std::vector<uint8_t *> data{};
        std::vector<uint32_t> linesize{};
        std::vector<uint8_t> data_internal{};
        uint32_t width{};
        uint32_t height{};
        uint64_t timestamp{};

        video_format format{};
        video_range_type range{};
        float color_matrix[16]{};
        float color_range_min[3]{};
        float color_range_max[3]{};
        bool flip{};
        bool flip_h{};

        lite_obs_source_video_frame2() {
            data.resize(MAX_AV_PLANES);
            linesize.resize(MAX_AV_PLANES);
        }
    };

    lite_source(source_type type, std::shared_ptr<lite_obs_core_video> c_v, std::shared_ptr<lite_obs_core_audio> c_a);
    ~lite_source();

    source_type lite_source_type();
    void lite_source_output_audio(const lite_obs_source_audio_frame &audio);
    void lite_source_output_video(const lite_obs_source_video_frame *frame);
    void lite_source_output_video(int texture_id, uint32_t texture_width, uint32_t texture_height);

    void lite_source_set_pos(float x, float y);
    void lite_source_set_scale(float width_scale, float height_scale);
    void lite_source_set_render_box(int x, int y, int width, int height, source_aspect_ratio_mode mode);

public:
    static std::recursive_mutex sources_mutex;
    static std::map<uintptr_t, std::map<uintptr_t, std::pair<source_type,std::shared_ptr<lite_source>>>> sources;

private:
    bool audio_pending();
    uint64_t audio_ts();

    void reset_resampler(const lite_obs_source_audio_frame &audio);
    bool copy_audio_data(const uint8_t *const data[], uint32_t frames, uint64_t ts);
    bool process_audio(const lite_obs_source_audio_frame &audio);

    void reset_audio_data(uint64_t os_time);
    void reset_audio_timing(uint64_t timestamp, uint64_t os_time);
    void handle_ts_jump(uint64_t expected, uint64_t ts, uint64_t diff, uint64_t os_time);
    void output_audio_data_internal(const struct audio_data *data);
    void output_audio_push_back(const struct audio_data *in, size_t channels);
    void output_audio_place(const struct audio_data *in, size_t channels, size_t sample_rate);

    float get_source_volume();
    void multiply_output_audio(size_t mix, size_t channels, float vol);
    void apply_audio_volume(uint32_t mixers, size_t channels, size_t sample_rate);
    void audio_source_tick(uint32_t mixers, size_t channels, size_t sample_rate, size_t size);
    void audio_render(uint32_t mixers, size_t channels, size_t sample_rate, size_t size);

    bool audio_buffer_insuffient(size_t sample_rate, uint64_t min_ts);
    void mix_audio(struct audio_output_data *mixes, size_t channels, size_t sample_rate, struct ts_info *ts);
    bool discard_if_stopped(size_t channels);
    void ignore_audio(size_t channels, size_t sample_rate);
    void discard_audio(int total_buffering_ticks, size_t channels, size_t sample_rate, struct ts_info *ts);

    //video
    void clean_cache();
    void free_async_cache();
    bool async_texture_changed(const lite_obs_source_video_frame *frame);
    void output_video_internal(const lite_obs_source_video_frame *frame);

    void remove_async_frame(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool ready_async_frame(uint64_t sys_time);
    std::shared_ptr<lite_obs_source_video_frame> get_closest_frame(uint64_t sys_time);
    bool set_packed422_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_packed444_alpha_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_planar444_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_planar444_alpha_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_planar420_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_planar420_alpha_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_planar422_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_planar422_alpha_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_nv12_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_y800_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_rgb_limited_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_bgr3_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool init_gpu_conversion(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    bool set_async_texture_size(const std::shared_ptr<lite_obs_source_video_frame> &frame);
    void async_tick(uint64_t sys_time);
    std::shared_ptr<lite_obs_source_video_frame> get_frame();
    bool update_async_texrender(const std::shared_ptr<lite_obs_source_video_frame> &frame, const std::vector<std::shared_ptr<gs_texture>> &tex, const std::shared_ptr<gs_texture_render> &texrender);
    bool update_async_textures(const std::shared_ptr<lite_obs_source_video_frame> &frame, const std::vector<std::shared_ptr<gs_texture>> &tex, const std::shared_ptr<gs_texture_render> &texrender);
    void update_async_video(uint64_t sys_time);
    bool render_crop_texture(const std::shared_ptr<gs_texture> &texture);
    void async_render();
    void render_texture(std::shared_ptr<gs_texture> texture);
    void render();

    void do_update_transform(const std::shared_ptr<gs_texture> &tex);

private:
    std::unique_ptr<lite_source_private> d_ptr{};
};

typedef void (*obs_source_audio_capture)(void *param, std::shared_ptr<lite_source> source, const audio_data *audio_data, bool muted);
