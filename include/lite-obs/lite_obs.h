#pragma once

#include "lite_obs_global.h"
#include "lite_obs_defines.h"
#include "lite_obs_callback.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lite_obs_media_source;
typedef struct lite_obs_media_source_api {
    struct lite_obs_media_source *obj;

    void (*output_audio)(struct lite_obs_media_source_api *source_api, const uint8_t *audio_data[MAX_AV_PLANES], uint32_t frames, audio_format format, speaker_layout layout, uint32_t sample_rate);
    void (*output_video)(struct lite_obs_media_source_api *source_api, int texId, uint32_t width, uint32_t height);
    void (*output_video2)(struct lite_obs_media_source_api *source_api, const uint8_t *video_data[MAX_AV_PLANES], const int line_size[MAX_AV_PLANES],
                          video_format format, video_range_type range,
                          video_colorspace color_space, uint32_t width,
                          uint32_t height);
    //only support RGBA format
    void (*output_video3)(struct lite_obs_media_source_api *source_api, const uint8_t *img_data, uint32_t img_width, uint32_t img_height);
    void (*clear_video)(struct lite_obs_media_source_api *source_api);

    void (*set_pos)(struct lite_obs_media_source_api *source_api, float x, float y);
    void (*set_scale)(struct lite_obs_media_source_api *source_api, float s_w, float s_h);
    void (*set_rotate)(struct lite_obs_media_source_api *source_api, float rotate);
    void (*set_render_box)(struct lite_obs_media_source_api *source_api, int x, int y, int width, int height, source_aspect_ratio_mode mode);
    void (*set_order)(struct lite_obs_media_source_api *source_api, order_movement movement);
    void (*set_flip)(struct lite_obs_media_source_api *source_api, bool flip_h, bool flip_v);
    void (*reset_transform)(struct lite_obs_media_source_api *source_api);
} lite_obs_media_source_api;

struct lite_obs;
typedef struct lite_obs_api {
    struct lite_obs *object;

    int (*lite_obs_reset_video)(struct lite_obs_api *core_api, uint32_t width, uint32_t height, uint32_t fps);
    bool (*lite_obs_reset_audio)(struct lite_obs_api *core_api, uint32_t sample_rate);

    bool (*lite_obs_start_output)(struct lite_obs_api *core_api, output_type type, void *output_info, int vb, int ab, struct lite_obs_output_callbak callback);
    void (*lite_obs_stop_output)(struct lite_obs_api *core_api);

    void (*lite_obs_reset_encoder)(struct lite_obs_api *core_api, bool sw);

} lite_obs_api;


LITE_OBS_API void lite_obs_set_log_handle(void (*log_callback)(int, const char *));

LITE_OBS_API lite_obs_api *lite_obs_api_new();
LITE_OBS_API void lite_obs_api_delete(lite_obs_api **obj);

LITE_OBS_API lite_obs_media_source_api *lite_obs_media_source_new(const lite_obs_api *api, source_type type);
LITE_OBS_API void lite_obs_media_source_delete(const lite_obs_api *api, lite_obs_media_source_api **source);

#ifdef __cplusplus
}
#endif
