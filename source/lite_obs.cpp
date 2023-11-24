#include "lite-obs/lite_obs_internal.h"
#include "lite-obs/util/log.h"
#include "lite-obs/lite_obs.h"

extern "C" {

struct lite_obs_media_source
{
    lite_obs_media_source_internal* source_internal{};
};

struct lite_obs {
    std::unique_ptr<lite_obs_internal> api_internal{};
};

void lite_obs_set_log_handle(void (*log_callback)(int, const char *))
{
    base_set_log_handler(log_callback);
}


lite_obs_media_source_api *lite_obs_media_source_new(const lite_obs_api *api, source_type type)
{
    if (!api)
        return nullptr;

    auto media_source_api = new lite_obs_media_source_api;
    media_source_api->obj = new lite_obs_media_source;
    media_source_api->obj->source_internal = api->object->api_internal->lite_obs_create_source(type);

    media_source_api->output_audio = [](struct lite_obs_media_source_api *source_api, const uint8_t *audio_data[MAX_AV_PLANES], uint32_t frames, audio_format format, speaker_layout layout, uint32_t sample_rate){
        source_api->obj->source_internal->output_audio(audio_data, frames, format, layout, sample_rate);
    };
    media_source_api->output_video = [](struct lite_obs_media_source_api *source_api, int texId, uint32_t width, uint32_t height){
        source_api->obj->source_internal->output_video(texId, width, height);
    };
    media_source_api->output_video2 = [](struct lite_obs_media_source_api *source_api, const uint8_t *video_data[MAX_AV_PLANES], const int line_size[MAX_AV_PLANES],
                                         video_format format, video_range_type range,
                                         video_colorspace color_space, uint32_t width,
                                         uint32_t height){
        source_api->obj->source_internal->output_video(video_data, line_size, format, range, color_space, width, height);
    };
    media_source_api->output_video3 = [](struct lite_obs_media_source_api *source_api, const uint8_t *img_data, uint32_t img_width, uint32_t img_height){
        source_api->obj->source_internal->output_video(img_data, img_width, img_height);
    };
    media_source_api->clear_video = [](struct lite_obs_media_source_api *source_api){
        source_api->obj->source_internal->clear_video();
    };
    media_source_api->set_pos = [](struct lite_obs_media_source_api *source_api, float x, float y){
        source_api->obj->source_internal->set_pos(x, y);
    };
    media_source_api->set_scale = [](struct lite_obs_media_source_api *source_api, float s_w, float s_h){
        source_api->obj->source_internal->set_scale(s_w, s_h);
    };
    media_source_api->set_render_box = [](struct lite_obs_media_source_api *source_api, int x, int y, int width, int height, source_aspect_ratio_mode mode){
        source_api->obj->source_internal->set_render_box(x, y, width, height, mode);
    };
    media_source_api->set_order = [](struct lite_obs_media_source_api *source_api, order_movement movement){
        source_api->obj->source_internal->set_order(movement);
    };

    return media_source_api;
}

void lite_obs_media_source_delete(const lite_obs_api *api, lite_obs_media_source_api **source)
{
    if (!api || !*source)
        return;

    api->object->api_internal->lite_obs_destroy_source((*source)->obj->source_internal);
    *source = nullptr;
}


lite_obs_api *lite_obs_api_new()
{
    auto api = new lite_obs_api;
    api->object = new lite_obs();
    api->object->api_internal = std::make_unique<lite_obs_internal>();

    api->lite_obs_reset_video = [](struct lite_obs_api *core_api, uint32_t width, uint32_t height, uint32_t fps){
        return core_api->object->api_internal->obs_reset_video(width, height, fps);
    };

    api->lite_obs_reset_audio = [](struct lite_obs_api *core_api, uint32_t sample_rate){
        return core_api->object->api_internal->obs_reset_audio(sample_rate);
    };

    api->lite_obs_start_output = [](struct lite_obs_api *core_api, const char *output_info, int vb, int ab, struct lite_obs_output_callbak callback){
        return core_api->object->api_internal->lite_obs_start_output(output_info, vb, ab, callback);
    };

    api->lite_obs_stop_output = [](struct lite_obs_api *core_api){
        core_api->object->api_internal->lite_obs_stop_output();
    };

    api->lite_obs_reset_encoder = [](struct lite_obs_api *core_api, bool sw){
        core_api->object->api_internal->lite_obs_reset_encoder(sw);
    };

    return api;
}

void lite_obs_api_delete(lite_obs_api **obj)
{
    if (!*obj)
        return;

    delete (*obj)->object;
    delete *obj;
    *obj = nullptr;
}

}
