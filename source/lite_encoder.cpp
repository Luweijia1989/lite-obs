#include "lite-obs/lite_encoder.h"

#include "lite-obs/lite_obs_core_video.h"
#include "lite-obs/lite_obs_core_audio.h"
#include "lite-obs/lite_output.h"
#include "lite-obs/media-io/video_output.h"
#include "lite-obs/media-io/audio_output.h"
#include "lite-obs/util/circlebuf.h"
#include "lite-obs/util/log.h"
#include <mutex>
#include <atomic>
#include <list>
#include <vector>

class lite_obs_output;

struct encoder_callback {
    bool sent_first_packet{};
    new_packet cb;
    void *param{};
};

struct lite_obs_encoder_private
{
    std::recursive_mutex init_mutex;

    int bitrate{};

    uint32_t samplerate{};
    size_t planes{};
    size_t blocksize{};
    size_t framesize{};
    size_t framesize_bytes{};

    size_t mixer_idx{};

    uint32_t scaled_width{};
    uint32_t scaled_height{};
    video_format preferred_format{};

    std::atomic_bool active{};
    bool initialized{};

    uint32_t timebase_num{};
    uint32_t timebase_den{};

    int64_t cur_pts{};

    circlebuf audio_input_buffer[MAX_AV_PLANES]{};
    uint8_t *audio_output_buffer[MAX_AV_PLANES]{};

    /* if a video encoder is paired with an audio encoder, make it start
         * up at the specific timestamp.  if this is the audio encoder,
         * wait_for_video makes it wait until it's ready to sync up with
         * video */
    bool wait_for_video{};
    bool first_received{};
    std::weak_ptr<lite_obs_encoder> paired_encoder{};
    int64_t offset_usec{};
    uint64_t first_raw_ts{};
    uint64_t start_ts{};

    std::mutex outputs_mutex;
    std::list<std::weak_ptr<lite_obs_output>> outputs{};

    bool destroy_on_stop{};

    /* stores the video/audio media output pointer.  video_t *or audio_t **/
    std::weak_ptr<video_output> v_media{};
    std::weak_ptr<audio_output> a_media{};
    std::weak_ptr<lite_obs_core_video> core_video{};
    std::weak_ptr<lite_obs_core_audio> core_audio{};

    std::recursive_mutex callbacks_mutex;
    std::list<encoder_callback> callbacks{};

    uint32_t sei_rate{};
    std::mutex sei_mutex;
    std::vector<uint8_t> custom_sei{};
    size_t custom_sei_size{};
    uint64_t sei_counting{};

    lite_obs_encoder_private() {
        custom_sei.resize(sizeof(uint8_t) * 1024 * 100);
    }
};

lite_obs_encoder::lite_obs_encoder(int bitrate, size_t mixer_idx)
{
    d_ptr = std::make_unique<lite_obs_encoder_private>();
    d_ptr->bitrate = bitrate;
    d_ptr->mixer_idx = mixer_idx;
}

lite_obs_encoder::~lite_obs_encoder()
{
    blog(LOG_DEBUG, "lite_obs_encoder destroyed.");
}

void lite_obs_encoder::get_audio_info_internal(audio_convert_info *info)
{
    auto ao = d_ptr->a_media.lock();
    if (!ao)
        return;

    const audio_output_info *aoi;
    aoi = ao->audio_output_get_info();

    if (info->format == audio_format::AUDIO_FORMAT_UNKNOWN)
        info->format = aoi->format;
    if (!info->samples_per_sec)
        info->samples_per_sec = aoi->samples_per_sec;
    if (info->speakers == speaker_layout::SPEAKERS_UNKNOWN)
        info->speakers = aoi->speakers;

    i_get_audio_info(info);
}

void lite_obs_encoder::reset_audio_buffers()
{
    free_audio_buffers();

    for (size_t i = 0; i < d_ptr->planes; i++)
        d_ptr->audio_output_buffer[i] = (uint8_t *)malloc(d_ptr->framesize_bytes);
}

void lite_obs_encoder::free_audio_buffers()
{
    for (size_t i = 0; i < MAX_AV_PLANES; i++) {
        circlebuf_free(&d_ptr->audio_input_buffer[i]);
        free(d_ptr->audio_output_buffer[i]);
        d_ptr->audio_output_buffer[i] = NULL;
    }
}

void lite_obs_encoder::intitialize_audio_encoder()
{
    audio_convert_info info{};
    i_get_audio_info(&info);

    d_ptr->samplerate = info.samples_per_sec;
    d_ptr->planes = get_audio_planes(info.format, info.speakers);
    d_ptr->blocksize = get_audio_size(info.format, info.speakers, 1);
    d_ptr->framesize = i_get_frame_size();

    d_ptr->framesize_bytes = d_ptr->blocksize * d_ptr->framesize;
    reset_audio_buffers();
}

bool lite_obs_encoder::obs_encoder_initialize_internal()
{
    if (d_ptr->active)
        return true;
    if (d_ptr->initialized)
        return true;

    obs_encoder_shutdown();

    if (!i_create())
        return false;

    if (i_encoder_type() == obs_encoder_type::OBS_ENCODER_AUDIO)
        intitialize_audio_encoder();

    d_ptr->initialized = true;
    return true;
}

bool lite_obs_encoder::obs_encoder_initialize()
{
    d_ptr->init_mutex.lock();
    auto success = obs_encoder_initialize_internal();
    d_ptr->init_mutex.unlock();

    return success;
}

void lite_obs_encoder::obs_encoder_shutdown()
{
    d_ptr->init_mutex.lock();

    if (i_encoder_valid()) {
        i_destroy();
        d_ptr->paired_encoder.reset();
        d_ptr->first_received = false;
        d_ptr->offset_usec = 0;
        d_ptr->start_ts = 0;
    }

    d_ptr->init_mutex.unlock();
}

auto lite_obs_encoder::get_callback_idx(new_packet cb, void *param)
{
    auto iter = d_ptr->callbacks.begin();
    for (; iter != d_ptr->callbacks.end(); iter++) {
        auto &callback = *iter;
        if (callback.cb == cb && callback.param == param)
            return iter;
    }

    return iter;
}

void lite_obs_encoder::clear_audio()
{
    for (size_t i = 0; i < d_ptr->planes; i++)
        circlebuf_free(&d_ptr->audio_input_buffer[i]);
}

size_t lite_obs_encoder::calc_offset_size(uint64_t v_start_ts, uint64_t a_start_ts)
{
    uint64_t offset = v_start_ts - a_start_ts;
    offset = (uint64_t)offset * (uint64_t)d_ptr->samplerate / 1000000000ULL;
    return (size_t)offset * d_ptr->blocksize;
}

void lite_obs_encoder::start_from_buffer(uint64_t v_start_ts)
{
    size_t size = d_ptr->audio_input_buffer[0].size;
    struct audio_data audio{};
    size_t offset_size = 0;

    for (size_t i = 0; i < MAX_AV_PLANES; i++) {
        audio.data[i] = (uint8_t *)(d_ptr->audio_input_buffer[i].data);
        memset(&d_ptr->audio_input_buffer[i], 0,
               sizeof(struct circlebuf));
    }

    if (d_ptr->first_raw_ts < v_start_ts)
        offset_size = calc_offset_size(v_start_ts, d_ptr->first_raw_ts);

    push_back_audio(&audio, size, offset_size);

    for (size_t i = 0; i < MAX_AV_PLANES; i++)
        free(audio.data[i]);
}

void lite_obs_encoder::push_back_audio(struct audio_data *data, size_t size, size_t offset_size)
{
    size -= offset_size;

    /* push in to the circular buffer */
    if (size)
        for (size_t i = 0; i < d_ptr->planes; i++)
            circlebuf_push_back(&d_ptr->audio_input_buffer[i], data->data[i] + offset_size, size);
}

bool lite_obs_encoder::buffer_audio(struct audio_data *data)
{
    size_t size = data->frames * d_ptr->blocksize;
    size_t offset_size = 0;
    bool success = true;

    auto paired_encoder = d_ptr->paired_encoder.lock();
    if (!d_ptr->start_ts && paired_encoder) {
        uint64_t end_ts = data->timestamp;
        uint64_t v_start_ts = paired_encoder->d_ptr->start_ts;

        /* no video yet, so don't start audio */
        if (!v_start_ts) {
            success = false;
            goto fail;
        }

        /* audio starting point still not synced with video starting
             * point, so don't start audio */
        end_ts += (uint64_t)data->frames * 1000000000ULL / (uint64_t)d_ptr->samplerate;
        if (end_ts <= v_start_ts) {
            success = false;
            goto fail;
        }

        /* ready to start audio, truncate if necessary */
        if (data->timestamp < v_start_ts)
            offset_size = calc_offset_size(v_start_ts, data->timestamp);
        if (data->timestamp <= v_start_ts)
            clear_audio();

        d_ptr->start_ts = v_start_ts;

        /* use currently buffered audio instead */
        if (v_start_ts < data->timestamp) {
            start_from_buffer(v_start_ts);
        }

    } else if (!d_ptr->start_ts && !paired_encoder) {
        d_ptr->start_ts = data->timestamp;
    }

fail:
    push_back_audio(data, size, offset_size);

    return success;
}

bool lite_obs_encoder::send_audio_data()
{
    struct encoder_frame enc_frame;

    memset(&enc_frame, 0, sizeof(struct encoder_frame));

    for (size_t i = 0; i < d_ptr->planes; i++) {
        circlebuf_pop_front(&d_ptr->audio_input_buffer[i],
                            d_ptr->audio_output_buffer[i],
                            d_ptr->framesize_bytes);

        enc_frame.data[i] = d_ptr->audio_output_buffer[i];
        enc_frame.linesize[i] = (uint32_t)d_ptr->framesize_bytes;
    }

    enc_frame.frames = (uint32_t)d_ptr->framesize;
    enc_frame.pts = d_ptr->cur_pts;

    if (!do_encode(&enc_frame))
        return false;

    d_ptr->cur_pts += d_ptr->framesize;
    return true;
}

void lite_obs_encoder::receive_audio_internal(size_t mix_idx, struct audio_data *data)
{
    struct audio_data audio = *data;

    if (!d_ptr->first_received) {
        d_ptr->first_raw_ts = audio.timestamp;
        d_ptr->first_received = true;
        clear_audio();
    }

    if (!buffer_audio(&audio))
        return;

    while (d_ptr->audio_input_buffer[0].size >= d_ptr->framesize_bytes) {
        if (!send_audio_data()) {
            break;
        }
    }
}

void lite_obs_encoder::receive_audio(void *param, size_t mix_idx, struct audio_data *data)
{
    auto encoder = (lite_obs_encoder*)param;
    encoder->receive_audio_internal(mix_idx, data);
}

void lite_obs_encoder::receive_video_internal(struct video_data *frame)
{
    auto pair = d_ptr->paired_encoder.lock();
    struct encoder_frame enc_frame;

    if (!d_ptr->first_received && pair) {
        if (!pair->d_ptr->first_received ||
                pair->d_ptr->first_raw_ts > frame->timestamp) {
            return;
        }
    }

    memset(&enc_frame, 0, sizeof(struct encoder_frame));

    for (size_t i = 0; i < MAX_AV_PLANES; i++) {
        enc_frame.data[i] = frame->frame.data[i];
        enc_frame.linesize[i] = frame->frame.linesize[i];
    }

    if (!d_ptr->start_ts)
        d_ptr->start_ts = frame->timestamp;

    enc_frame.frames = 1;
    enc_frame.pts = d_ptr->cur_pts;

    if (do_encode(&enc_frame))
        d_ptr->cur_pts += d_ptr->timebase_num;
}

void lite_obs_encoder::receive_video(void *param, struct video_data *frame)
{
    auto encoder = (lite_obs_encoder*)param;
    encoder->receive_video_internal(frame);
}

void lite_obs_encoder::add_connection()
{
    if (i_encoder_type() == obs_encoder_type::OBS_ENCODER_AUDIO) {
        struct audio_convert_info audio_info = {0};
        get_audio_info_internal(&audio_info);
        auto ao = d_ptr->a_media.lock();
        if (ao)
            ao->audio_output_connect(d_ptr->mixer_idx, &audio_info, lite_obs_encoder::receive_audio, this);
    } else {
        video_scale_info info{};
        i_get_video_info(&info);

        if (i_gpu_encode_available()) {
            start_gpu_encode();
        } else {
            d_ptr->core_video.lock()->lite_obs_core_video_change_raw_active(true);
            auto vo = d_ptr->v_media.lock();
            if (vo)
                vo->video_output_connect(&info, lite_obs_encoder::receive_video, this);
        }
    }

    d_ptr->active = true;
}

void lite_obs_encoder::remove_connection(bool shutdown)
{
    if (i_encoder_type() == obs_encoder_type::OBS_ENCODER_AUDIO) {
        auto ao = d_ptr->a_media.lock();
        ao->audio_output_disconnect(d_ptr->mixer_idx, lite_obs_encoder::receive_audio, this);
    } else {
        if (i_gpu_encode_available()) {
            stop_gpu_encode();
        } else {
            d_ptr->core_video.lock()->lite_obs_core_video_change_raw_active(false);
            auto vo = d_ptr->v_media.lock();
            if (vo)
                vo->video_output_disconnect(lite_obs_encoder::receive_video, this);
        }
    }

    /* obs_encoder_shutdown locks init_mutex, so don't call it on encode
         * errors, otherwise you can get a deadlock with outputs when they end
         * data capture, which will lock init_mutex and the video callback
         * mutex in the reverse order.  instead, call shutdown before starting
         * up again */
    if (shutdown)
        obs_encoder_shutdown();

    d_ptr->active = false;
}

void lite_obs_encoder::obs_encoder_start_internal(new_packet cb, void *param)
{
    encoder_callback callback = {false, cb, param};

    if (!i_encoder_valid())
        return;

    d_ptr->callbacks_mutex.lock();

    auto first = (d_ptr->callbacks.size() == 0);

    auto idx = get_callback_idx(cb, param);
    if (idx == d_ptr->callbacks.end())
        d_ptr->callbacks.push_back(callback);

    d_ptr->callbacks_mutex.unlock();

    if (first) {
        d_ptr->cur_pts = 0;
        add_connection();
    }
}

void lite_obs_encoder::obs_encoder_start(new_packet cb, void *param)
{
    if (!cb) {
        blog(LOG_DEBUG, "obs_encoder_start error, invalid callback.");
    }

    d_ptr->init_mutex.lock();
    obs_encoder_start_internal(cb, param);
    d_ptr->init_mutex.unlock();
}

void lite_obs_encoder::obs_encoder_actually_destroy()
{
    d_ptr->outputs_mutex.lock();
    for (auto iter = d_ptr->outputs.begin(); iter != d_ptr->outputs.end(); iter++) {
        auto output = (*iter).lock();
        if (output)
            output->lite_obs_output_remove_encoder(shared_from_this());
    }
    d_ptr->outputs.clear();
    d_ptr->outputs_mutex.unlock();

    blog(LOG_DEBUG, "encoder destroyed");

    free_audio_buffers();

    if (i_encoder_valid())
        i_destroy();
    d_ptr->callbacks.clear();
    d_ptr.reset();
}

bool lite_obs_encoder::obs_encoder_stop_internal(new_packet cb, void *param)
{
    bool last = false;

    d_ptr->callbacks_mutex.lock();

    auto idx = get_callback_idx(cb, param);
    if (idx != d_ptr->callbacks.end()) {
        d_ptr->callbacks.erase(idx);
        last = (d_ptr->callbacks.size() == 0);
    }

    d_ptr->callbacks_mutex.unlock();

    if (last) {
        remove_connection(true);
        d_ptr->initialized = false;

        if (d_ptr->destroy_on_stop) {
            d_ptr->init_mutex.unlock();
            obs_encoder_actually_destroy();
            return true;
        }
    }

    return false;
}

void lite_obs_encoder::obs_encoder_stop(new_packet cb, void *param)
{
    if (!cb) {
        blog(LOG_DEBUG, "obs_encoder_stop error, invalid callback.");
        return;
    }

    d_ptr->init_mutex.lock();
    auto destroyed = obs_encoder_stop_internal(cb, param);
    if (!destroyed)
        d_ptr->init_mutex.unlock();
}

void lite_obs_encoder::obs_encoder_add_output(std::shared_ptr<lite_obs_output> output)
{
    d_ptr->outputs_mutex.lock();
    d_ptr->outputs.push_back(output);
    d_ptr->outputs_mutex.unlock();
}

void lite_obs_encoder::obs_encoder_remove_output(std::shared_ptr<lite_obs_output> output)
{
    d_ptr->outputs_mutex.lock();
    for (auto iter = d_ptr->outputs.begin(); iter != d_ptr->outputs.end(); iter++) {
        auto o = (*iter).lock();
        if (o == output) {
            d_ptr->outputs.erase(iter);
            break;
        }
    }
    d_ptr->outputs_mutex.unlock();
}

bool lite_obs_encoder::start_gpu_encode()
{
    return true;
}

void lite_obs_encoder::stop_gpu_encode()
{

}

bool lite_obs_encoder::do_encode(encoder_frame *frame)
{
    auto pkt = std::make_shared<encoder_packet>();
    pkt->timebase_num = d_ptr->timebase_num;
    pkt->timebase_den = d_ptr->timebase_den;
    pkt->encoder = shared_from_this();

    auto send = [this](std::shared_ptr<encoder_packet> pkt){
        if (!d_ptr->first_received) {
            d_ptr->offset_usec = packet_dts_usec(pkt);
            d_ptr->first_received = true;
        }

        /* we use system time here to ensure sync with other encoders,
             * you do not want to use relative timestamps here */
        pkt->dts_usec = d_ptr->start_ts / 1000 +
                packet_dts_usec(pkt) - d_ptr->offset_usec;
        pkt->sys_dts_usec = pkt->dts_usec;

        d_ptr->callbacks_mutex.lock();
        for (auto iter = d_ptr->callbacks.begin(); iter != d_ptr->callbacks.end(); iter++) {
            auto &cb = *iter;
            send_packet(&cb, pkt);
        }
        d_ptr->callbacks_mutex.unlock();
    };

    auto success = i_encode(frame, pkt, send);
    if (!success) {
        blog(LOG_ERROR, "Error encoding with encoder");
        full_stop();
    }

    return success;
}

void lite_obs_encoder::full_stop()
{
    d_ptr->outputs_mutex.lock();
    for (auto iter=d_ptr->outputs.begin(); iter != d_ptr->outputs.end(); iter++) {
        auto output = (*iter).lock();
        if (!output)
            continue;

        output->lite_obs_output_stop();
        output->lite_obs_output_flush_packet();
    }
    d_ptr->outputs_mutex.unlock();

    d_ptr->callbacks_mutex.lock();
    d_ptr->callbacks.clear();
    d_ptr->callbacks_mutex.unlock();

    remove_connection(false);
    d_ptr->initialized = false;
}

void lite_obs_encoder::send_first_video_packet(encoder_callback *cb, std::shared_ptr<encoder_packet> packet)
{
    uint8_t *sei;
    size_t size;

    /* always wait for first keyframe */
    if (!packet->keyframe)
        return;

    if (!i_get_sei_data(&sei, &size) || !sei || !size) {
        cb->cb(cb->param, packet);
        cb->sent_first_packet = true;
        return;
    }

    auto data = std::make_shared<std::vector<uint8_t>>();
    data->resize(size + packet->data->size());
    memcpy(data->data(), sei, size);
    memcpy(data->data() + size, packet->data->data(), packet->data->size());

    auto first_packet = packet;
    first_packet->data = data;

    cb->cb(cb->param, first_packet);
    cb->sent_first_packet = true;
}

void lite_obs_encoder::send_packet(encoder_callback *cb, std::shared_ptr<encoder_packet> packet)
{
    if (i_encoder_type() == obs_encoder_type::OBS_ENCODER_VIDEO && !cb->sent_first_packet)
        send_first_video_packet(cb, packet);
    else
        cb->cb(cb->param, packet);
}

void lite_obs_encoder::obs_encoder_destroy()
{
    d_ptr->init_mutex.lock();
    d_ptr->callbacks_mutex.lock();
    auto d = d_ptr->callbacks.size() == 0;
    if (!d)
        d_ptr->destroy_on_stop = true;
    d_ptr->init_mutex.unlock();
    d_ptr->callbacks_mutex.unlock();

    if (d)
        obs_encoder_actually_destroy();
}

void lite_obs_encoder::lite_obs_encoder_update_bitrate(int bitrate)
{
    d_ptr->bitrate = bitrate;
}

int lite_obs_encoder::lite_obs_encoder_bitrate()
{
    return d_ptr->bitrate;
}

void lite_obs_encoder::lite_obs_encoder_set_scaled_size(uint32_t width, uint32_t height)
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_VIDEO)
        return;

    if (d_ptr->active) {
        blog(LOG_WARNING, "encoder Cannot set the scaled resolution while the encoder is active");
        return;
    }

    d_ptr->scaled_width = width;
    d_ptr->scaled_height = height;
}

bool lite_obs_encoder::lite_obs_encoder_scaling_enabled()
{
    return d_ptr->scaled_width || d_ptr->scaled_height;
}

uint32_t lite_obs_encoder::lite_obs_encoder_get_width()
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_VIDEO) {
        blog(LOG_WARNING, "obs_encoder_get_width:  encoder is not a video encoder");
        return 0;
    }

    auto vo = d_ptr->v_media.lock();
    if (!vo)
        return 0;

    return d_ptr->scaled_width != 0 ? d_ptr->scaled_width : vo->video_output_get_width();
}

uint32_t lite_obs_encoder::lite_obs_encoder_get_height()
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_VIDEO) {
        blog(LOG_WARNING, "obs_encoder_get_height:  encoder is not a video encoder");
        return 0;
    }

    auto vo = d_ptr->v_media.lock();
    if (!vo)
        return 0;

    return d_ptr->scaled_height != 0 ? d_ptr->scaled_height : vo->video_output_get_height();
}

uint32_t lite_obs_encoder::lite_obs_encoder_get_sample_rate()
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_AUDIO) {
        blog(LOG_WARNING, "lite_obs_encoder_get_sample_rate: encoder is not a audio encoder");
        return 0;
    }

    auto ao = d_ptr->a_media.lock();
    if (!ao)
        return 0;

    return d_ptr->samplerate != 0 ? d_ptr->samplerate : ao->audio_output_get_sample_rate();
}

size_t lite_obs_encoder::lite_obs_encoder_get_frame_size()
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_AUDIO) {
        blog(LOG_WARNING, "lite_obs_encoder_get_frame_size: encoder is not a audio encoder");
        return 0;
    }

    return d_ptr->framesize;
}

void lite_obs_encoder::lite_obs_encoder_set_preferred_video_format(video_format format)
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_VIDEO)
        return;

    d_ptr->preferred_format = format;
}

video_format lite_obs_encoder::lite_obs_encoder_get_preferred_video_format()
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_VIDEO)
        return video_format::VIDEO_FORMAT_NONE;

    return d_ptr->preferred_format;
}

bool lite_obs_encoder::lite_obs_encoder_get_extra_data(uint8_t **extra_data, size_t *size)
{
    return i_get_extra_data(extra_data, size);
}

void lite_obs_encoder::lite_obs_encoder_set_core_video(std::shared_ptr<lite_obs_core_video> c_v)
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_VIDEO) {
        blog(LOG_WARNING, "obs_encoder_set_video: encoder is not a video encoder");
        return;
    }

    if (!c_v)
        return;

    auto voi = c_v->core_video()->video_output_get_info();

    d_ptr->core_video = c_v;
    d_ptr->v_media = c_v->core_video();
    d_ptr->timebase_num = voi->fps_den;
    d_ptr->timebase_den = voi->fps_num;
}

void lite_obs_encoder::lite_obs_encoder_set_core_audio(std::shared_ptr<lite_obs_core_audio> c_a)
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_AUDIO) {
        blog(LOG_WARNING, "lite_obs_encoder_set_audio: encoder is not a audio encoder");
        return;
    }

    if (!c_a)
        return;

    d_ptr->core_audio = c_a;
    d_ptr->a_media = c_a->core_audio();
    d_ptr->timebase_num = 1;
    d_ptr->timebase_den = d_ptr->a_media.lock()->audio_output_get_sample_rate();
}

std::shared_ptr<video_output> lite_obs_encoder::lite_obs_encoder_video()
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_VIDEO) {
        blog(LOG_WARNING, "lite_obs_encoder_video: encoder is not a video encoder");
        return nullptr;
    }

    return d_ptr->v_media.lock();
}

std::shared_ptr<audio_output> lite_obs_encoder::lite_obs_encoder_audio()
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_AUDIO  ) {
        blog(LOG_WARNING, "lite_obs_encoder_audio: encoder is not a audio encoder");
        return nullptr;
    }

    return d_ptr->a_media.lock();
}

bool lite_obs_encoder::lite_obs_encoder_active()
{
    return d_ptr->active;
}

std::shared_ptr<lite_obs_encoder> lite_obs_encoder::lite_obs_encoder_paired_encoder()
{
    return d_ptr->paired_encoder.lock();
}

void lite_obs_encoder::lite_obs_encoder_set_paired_encoder(std::shared_ptr<lite_obs_encoder> encoder)
{
    d_ptr->paired_encoder = encoder;
}

void lite_obs_encoder::lite_obs_encoder_set_lock(bool lock)
{
    if (lock)
        d_ptr->init_mutex.lock();
    else
        d_ptr->init_mutex.unlock();
}

void lite_obs_encoder::lite_obs_encoder_set_wait_for_video(bool wait)
{
    if (i_encoder_type() != obs_encoder_type::OBS_ENCODER_AUDIO)
        return;

    d_ptr->wait_for_video = wait;
}

void lite_obs_encoder::lite_obs_encoder_set_sei(char *sei, int len)
{

}

void lite_obs_encoder::lite_obs_encoder_clear_sei()
{

}

bool lite_obs_encoder::lite_obs_encoder_get_sei(uint8_t *sei, int *sei_len)
{
    return false;
}

void lite_obs_encoder::lite_obs_encoder_set_sei_rate(uint32_t rate)
{
    d_ptr->sei_rate = rate;
}

uint32_t lite_obs_encoder::lite_obs_encoder_get_sei_rate()
{
    return d_ptr->sei_rate;
}

