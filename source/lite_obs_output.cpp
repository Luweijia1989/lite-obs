#include "lite-obs/lite_obs_output.h"
#include "lite-obs/util/threading.h"
#include "lite-obs/util/circlebuf.h"
#include "lite-obs/util/log.h"
#include "lite-obs/lite_encoder_info.h"
#include "lite-obs/lite_encoder.h"
#include "lite-obs/lite_obs_internal.h"
#include "lite-obs/lite_obs_core_video.h"
#include "lite-obs/lite_obs_core_audio.h"
#include "lite-obs/media-io/video_output.h"
#include "lite-obs/media-io/audio_output.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <list>

struct lite_obs_output_private
{
    lite_obs_output_callbak signal_callback{};

    bool received_video{};
    bool received_audio{};
    std::atomic_bool data_active{};
    int64_t video_offset{};
    int64_t audio_offsets[MAX_AUDIO_MIXES]{};
    int64_t highest_audio_ts{};
    int64_t highest_video_ts{};
    std::thread end_data_capture_thread;
    os_event_t *stopping_event{};
    std::mutex interleaved_mutex;
    std::list<std::shared_ptr<encoder_packet>> interleaved_packets;
    int stop_code{};

    int reconnect_retry_sec{};
    int reconnect_retry_max{};
    int reconnect_retries{};
    int reconnect_retry_cur_sec{};
    std::thread reconnect_thread;
    os_event_t *reconnect_stop_event{};
    std::atomic_bool reconnecting{};

    uint32_t starting_drawn_count{};
    uint32_t starting_lagged_count{};
    uint32_t starting_frame_count{};

    int total_frames{};

    std::atomic_bool active{};
    std::weak_ptr<video_output> video{};
    std::weak_ptr<audio_output> audio{};
    std::weak_ptr<lite_obs_core_video> core_video{};
    std::weak_ptr<lite_obs_core_audio> core_audio{};
    std::weak_ptr<lite_obs_encoder> video_encoder{};
    std::weak_ptr<lite_obs_encoder> audio_encoders[MAX_AUDIO_MIXES]{};

    circlebuf audio_buffer[MAX_AUDIO_MIXES][MAX_AV_PLANES]{};
    uint64_t audio_start_ts{};
    uint64_t video_start_ts{};
    size_t audio_size{};
    size_t planes{};
    size_t sample_rate{};
    size_t total_audio_frames{};

    uint32_t scaled_width{};
    uint32_t scaled_height{};

    bool video_conversion_set{};
    bool audio_conversion_set{};
    video_scale_info video_conversion{};
    audio_convert_info audio_conversion{};

    std::string last_error_message{};

    float audio_data[MAX_AUDIO_CHANNELS][AUDIO_OUTPUT_FRAMES]{};

    uint32_t sei_count_per_second{};

    lite_obs_output_private() {
        os_event_init(&stopping_event, OS_EVENT_TYPE_MANUAL);
        os_event_signal(stopping_event);
        os_event_init(&reconnect_stop_event, OS_EVENT_TYPE_MANUAL);
    }

    ~lite_obs_output_private() {
        os_event_destroy(stopping_event);
        os_event_destroy(reconnect_stop_event);

        if (reconnect_thread.joinable())
            reconnect_thread.join();
    }
};

lite_obs_output::lite_obs_output()
{
    d_ptr = std::make_unique<lite_obs_output_private>();
}

lite_obs_output::~lite_obs_output()
{

}

bool lite_obs_output::stopping()
{
    return os_event_try(d_ptr->stopping_event) == EAGAIN;
}

void lite_obs_output::free_packets()
{
    d_ptr->interleaved_packets.clear();
}

void lite_obs_output::set_output_signal_callback(lite_obs_output_callbak callback)
{
    d_ptr->signal_callback = callback;
}

const lite_obs_output_callbak &lite_obs_output::output_signal_callback()
{
    return d_ptr->signal_callback;
}

bool lite_obs_output::lite_obs_output_create(std::shared_ptr<lite_obs_core_video> c_v, std::shared_ptr<lite_obs_core_audio> c_a)
{
    d_ptr->core_video = c_v;
    d_ptr->core_audio = c_a;
    d_ptr->video = c_v->core_video();
    d_ptr->audio = c_a->core_audio();

    d_ptr->reconnect_retry_sec = 2;
    d_ptr->reconnect_retry_max = 20;

    if (!i_create()) {
        blog(LOG_ERROR, "Failed to create output!");
        return false;
    }

    blog(LOG_DEBUG, "output created.");
    return true;
}

void lite_obs_output::clear_audio_buffers()
{
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        for (size_t j = 0; j < MAX_AV_PLANES; j++) {
            circlebuf_free(&d_ptr->audio_buffer[i][j]);
        }
    }
}

void lite_obs_output::lite_obs_output_destroy()
{
    blog(LOG_DEBUG, "output destroyed");

    if (i_output_valid() && d_ptr->active)
        obs_output_stop_internal();

    os_event_wait(d_ptr->stopping_event);
    if (d_ptr->end_data_capture_thread.joinable())
        d_ptr->end_data_capture_thread.join();

    if (i_output_valid())
        i_destroy();

    free_packets();

    auto video_encoder = d_ptr->video_encoder.lock();
    if (video_encoder) {
        video_encoder->obs_encoder_remove_output(shared_from_this());
    }

    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        auto audio_encoder = d_ptr->audio_encoders[i].lock();
        if (audio_encoder) {
            audio_encoder->obs_encoder_remove_output(shared_from_this());
        }
    }

    clear_audio_buffers();
}

bool lite_obs_output::lite_obs_output_actual_start()
{
    bool success = false;

    os_event_wait(d_ptr->stopping_event);
    d_ptr->stop_code = 0;
    d_ptr->last_error_message.clear();

    if (i_output_valid())
        success = i_start();

    auto vo = d_ptr->video.lock();
    if (success && vo) {
        d_ptr->starting_frame_count = vo->video_output_get_total_frames();
        auto core_video = d_ptr->core_video.lock();
        d_ptr->starting_drawn_count = core_video->total_frames();
        d_ptr->starting_lagged_count = core_video->lagged_frames();
    }

    return success;
}

bool lite_obs_output::lite_obs_output_start()
{
    if (!i_output_valid())
        return false;

    if (d_ptr->active) {
        return true;
    }

    if (lite_obs_output_actual_start()) {
        if (d_ptr->signal_callback.start) {
            d_ptr->signal_callback.starting(d_ptr->signal_callback.opaque);
        }
        return true;
    }

    return false;
}

void lite_obs_output::obs_output_stop_internal()
{
    os_event_reset(d_ptr->stopping_event);

    bool was_reconnecting = d_ptr->reconnecting;
    if (d_ptr->reconnecting) {
        os_event_signal(d_ptr->reconnect_stop_event);
    }
    //always check joinable
    if (d_ptr->reconnect_thread.joinable())
        d_ptr->reconnect_thread.join();

    if (i_output_valid()) {
        i_stop(0);
    } else if (was_reconnecting) {
        d_ptr->stop_code = LITE_OBS_OUTPUT_SUCCESS;
        if (d_ptr->signal_callback.stop)
            d_ptr->signal_callback.stop(d_ptr->stop_code, d_ptr->last_error_message.c_str(), d_ptr->signal_callback.opaque);
        os_event_signal(d_ptr->stopping_event);
    }
}

void lite_obs_output::lite_obs_output_stop()
{
    if (!i_output_valid() || !d_ptr->active)
        return;

    if (!stopping()) {
        d_ptr->stop_code = 0;
        if (d_ptr->signal_callback.stopping)
            d_ptr->signal_callback.stopping(d_ptr->signal_callback.opaque);
    }
    obs_output_stop_internal();
}

std::shared_ptr<video_output> lite_obs_output::lite_obs_output_video()
{
    return d_ptr->video.lock();
}
std::shared_ptr<audio_output> lite_obs_output::lite_obs_output_audio()
{
    return d_ptr->audio.lock();
}

void lite_obs_output::lite_obs_output_set_video_encoder(std::shared_ptr<lite_obs_encoder> encoder)
{
    if (!encoder)
        return;

    if (encoder->lite_obs_encoder_type() != obs_encoder_type::OBS_ENCODER_VIDEO) {
        blog(LOG_WARNING, "obs_output_set_video_encoder: encoder passed is not a video encoder");
        return;
    }

    auto video_encoder = d_ptr->video_encoder.lock();

    if (video_encoder == encoder)
        return;

    if (video_encoder)
        video_encoder->obs_encoder_remove_output(shared_from_this());

    encoder->obs_encoder_add_output(shared_from_this());
    d_ptr->video_encoder = encoder;

    if (d_ptr->scaled_width && d_ptr->scaled_height)
        encoder->lite_obs_encoder_set_scaled_size(d_ptr->scaled_width, d_ptr->scaled_height);
}

void lite_obs_output::lite_obs_output_set_audio_encoder(std::shared_ptr<lite_obs_encoder> encoder, size_t idx)
{
    if (!encoder)
        return;

    if (encoder->lite_obs_encoder_type() != obs_encoder_type::OBS_ENCODER_AUDIO) {
        blog(LOG_WARNING, "obs_output_set_audio_encoder: encoder passed is not a audio encoder");
        return;
    }

    if (idx > 0) {
        return;
    }

    auto audio_encoder = d_ptr->audio_encoders[idx].lock();
    if (audio_encoder == encoder)
        return;

    if (audio_encoder)
        audio_encoder->obs_encoder_remove_output(shared_from_this());

    encoder->obs_encoder_add_output(shared_from_this());
    d_ptr->audio_encoders[idx] = encoder;
}

std::shared_ptr<lite_obs_encoder> lite_obs_output::lite_obs_output_get_video_encoder()
{
    return d_ptr->video_encoder.lock();
}

std::shared_ptr<lite_obs_encoder> lite_obs_output::lite_obs_output_get_audio_encoder(size_t idx)
{
    return d_ptr->audio_encoders[idx].lock();
}

uint64_t lite_obs_output::lite_obs_output_get_total_bytes()
{
    return i_get_total_bytes();
}

int lite_obs_output::lite_obs_output_get_frames_dropped()
{
    return i_get_dropped_frames();
}

int lite_obs_output::lite_obs_output_get_total_frames()
{
    return d_ptr->total_frames;
}

void lite_obs_output::lite_obs_output_set_preferred_size(uint32_t width, uint32_t height)
{
    if (!i_has_video())
        return;

    if (d_ptr->active) {
        blog(LOG_WARNING, "output : Cannot set the preferred resolution while the output is active");
        return;
    }

    d_ptr->scaled_width = width;
    d_ptr->scaled_height = height;

    if (i_encoded()) {
        auto vc = d_ptr->video_encoder.lock();
        if (vc)
            vc->lite_obs_encoder_set_scaled_size(width, height);
    }
}

uint32_t lite_obs_output::lite_obs_output_get_width()
{
    if (!i_has_video())
        return 0;

    if (i_encoded()) {
        auto vc = d_ptr->video_encoder.lock();
        if (vc)
            return vc->lite_obs_encoder_get_width();
        else
            return 0;
    } else {
        if (d_ptr->scaled_width != 0)
            return d_ptr->scaled_width;
        else {
            auto vo = d_ptr->video.lock();
            if (vo)
                return vo->video_output_get_width();
            else
                return 0;
        }
    }
}

uint32_t lite_obs_output::lite_obs_output_get_height()
{
    if (!i_has_video())
        return 0;

    if (i_encoded()) {
        auto vc = d_ptr->video_encoder.lock();
        if (vc)
            return vc->lite_obs_encoder_get_height();
        else
            return 0;
    } else {
        if (d_ptr->scaled_height != 0)
            return d_ptr->scaled_height;
        else {
            auto vo = d_ptr->video.lock();
            if (vo)
                return vo->video_output_get_height();
            else
                return 0;
        }
    }
}

void lite_obs_output::lite_obs_output_set_video_conversion(const video_scale_info *conversion)
{
    if (!conversion)
        return;

    d_ptr->video_conversion = *conversion;
    d_ptr->video_conversion_set = true;
}

void lite_obs_output::lite_obs_output_set_audio_conversion(const audio_convert_info *conversion)
{
    if (!conversion)
        return;

    d_ptr->audio_conversion = *conversion;
    d_ptr->audio_conversion_set = true;
}

bool lite_obs_output::can_begin_data_capture(bool encoded, bool has_video, bool has_audio)
{
    if (has_video) {
        if (encoded) {
            if (!d_ptr->video_encoder.lock())
                return false;
        } else {
            if (!d_ptr->video.lock())
                return false;
        }
    }

    if (has_audio) {
        if (encoded) {
            if (!d_ptr->audio_encoders[0].lock())
                return false;
        } else {
            if (!d_ptr->audio.lock())
                return false;
        }
    }

    return true;
}

bool lite_obs_output::lite_obs_output_can_begin_data_capture()
{
    if (d_ptr->active)
        return false;

    if (d_ptr->end_data_capture_thread.joinable())
        d_ptr->end_data_capture_thread.join();

    return can_begin_data_capture(i_encoded(), i_has_video(), i_has_audio());
}

bool lite_obs_output::lite_obs_output_initialize_encoders()
{
    if (d_ptr->active) {
        return false;
    }

    if (!i_encoded())
        return false;
    if (i_has_video()) {
        auto vc = d_ptr->video_encoder.lock();
        if (!vc)
            return false;

        if (!vc->obs_encoder_initialize())
            return false;
    }
    if (i_has_audio()) {
        auto ac = d_ptr->audio_encoders[0].lock();
        if (!ac)
            return false;

        if (!ac->obs_encoder_initialize())
            return false;
    }

    return true;
}

void lite_obs_output::reset_raw_output()
{
    clear_audio_buffers();

    auto ao = d_ptr->audio.lock();
    if (ao) {
        auto *aoi = ao->audio_output_get_info();
        struct audio_convert_info conv = d_ptr->audio_conversion;
        struct audio_convert_info info = {aoi->samples_per_sec, aoi->format, aoi->speakers, };

        if (d_ptr->audio_conversion_set) {
            if (conv.samples_per_sec)
                info.samples_per_sec = conv.samples_per_sec;
            if (conv.format != audio_format::AUDIO_FORMAT_UNKNOWN)
                info.format = conv.format;
            if (conv.speakers != speaker_layout::SPEAKERS_UNKNOWN)
                info.speakers = conv.speakers;
        }

        d_ptr->sample_rate = info.samples_per_sec;
        d_ptr->planes = get_audio_planes(info.format, info.speakers);
        d_ptr->total_audio_frames = 0;
        d_ptr->audio_size = get_audio_size(info.format, info.speakers, 1);
    }

    d_ptr->audio_start_ts = 0;
    d_ptr->video_start_ts = 0;
}

void lite_obs_output::pair_encoders()
{
    auto video = d_ptr->video_encoder.lock();
    auto audio = d_ptr->audio_encoders[0].lock();
    if (video && audio && !audio->lite_obs_encoder_active() && !audio->lite_obs_encoder_paired_encoder()) {
        audio->lite_obs_encoder_set_lock(true);
        video->lite_obs_encoder_set_lock(true);

        if (!audio->lite_obs_encoder_active() && !video->lite_obs_encoder_active() &&
                !video->lite_obs_encoder_paired_encoder() && !audio->lite_obs_encoder_paired_encoder()) {

            audio->lite_obs_encoder_set_wait_for_video(true);
            audio->lite_obs_encoder_set_paired_encoder(video);
            video->lite_obs_encoder_set_paired_encoder(audio);
        }

        video->lite_obs_encoder_set_lock(false);
        audio->lite_obs_encoder_set_lock(false);
    }
}

void lite_obs_output::reset_packet_data()
{
    d_ptr->received_audio = false;
    d_ptr->received_video = false;
    d_ptr->highest_audio_ts = 0;
    d_ptr->highest_video_ts = 0;
    d_ptr->video_offset = 0;

    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++)
        d_ptr->audio_offsets[i] = 0;

    free_packets();
}

bool lite_obs_output::has_scaling()
{
    auto vo = d_ptr->video.lock();
    if (vo) {
        uint32_t video_width = vo->video_output_get_width();
        uint32_t video_height = vo->video_output_get_height();

        return d_ptr->scaled_width && d_ptr->scaled_height && (video_width != d_ptr->scaled_width || video_height != d_ptr->scaled_height);
    }

    return false;
}

video_scale_info *lite_obs_output::get_video_conversion()
{
    if (d_ptr->video_conversion_set) {
        if (!d_ptr->video_conversion.width)
            d_ptr->video_conversion.width = lite_obs_output_get_width();

        if (!d_ptr->video_conversion.height)
            d_ptr->video_conversion.height = lite_obs_output_get_height();

        return &d_ptr->video_conversion;

    } else if (has_scaling()) {
        auto vo = d_ptr->video.lock();
        if (!vo)
            return nullptr;

        const struct video_output_info *info = vo->video_output_get_info();

        d_ptr->video_conversion.format = info->format;
        d_ptr->video_conversion.colorspace = video_colorspace::VIDEO_CS_DEFAULT;
        d_ptr->video_conversion.range = video_range_type::VIDEO_RANGE_DEFAULT;
        d_ptr->video_conversion.width = d_ptr->scaled_width;
        d_ptr->video_conversion.height = d_ptr->scaled_height;
        return &d_ptr->video_conversion;
    }

    return nullptr;
}

audio_convert_info *lite_obs_output::get_audio_conversion()
{
    return d_ptr->audio_conversion_set ? &d_ptr->audio_conversion : NULL;
}

void lite_obs_output::discard_to_idx(size_t idx)
{
    if (idx <= 0)
        return;

    for (size_t i = 0; i < idx; ++i) {
        d_ptr->interleaved_packets.pop_front();
    }
}

void lite_obs_output::discard_unused_audio_packets(int64_t dts_usec)
{
    size_t idx = 0;

    for (auto iter = d_ptr->interleaved_packets.begin(); iter != d_ptr->interleaved_packets.end(); iter++) {
        auto p = *iter;
        if (p->dts_usec >= dts_usec)
            break;
        idx++;
    }

    if (idx)
        discard_to_idx(idx);
}

void lite_obs_output::apply_interleaved_packet_offset(std::shared_ptr<struct encoder_packet>out)
{
    int64_t offset;

    /* audio and video need to start at timestamp 0, and the encoders
     * may not currently be at 0 when we get data.  so, we store the
     * current dts as offset and subtract that value from the dts/pts
     * of the output packet. */
    offset = (out->type == obs_encoder_type::OBS_ENCODER_VIDEO)
            ? d_ptr->video_offset
            : d_ptr->audio_offsets[out->track_idx];

    out->dts -= offset;
    out->pts -= offset;

    /* convert the newly adjusted dts to relative dts time to ensure proper
     * interleaving.  if we're using an audio encoder that's already been
     * started on another output, then the first audio packet may not be
     * quite perfectly synced up in terms of system time (and there's
     * nothing we can really do about that), but it will always at least be
     * within a 23ish millisecond threshold (at least for AAC) */
    out->dts_usec = packet_dts_usec(out);
}

void lite_obs_output::check_received(std::shared_ptr<encoder_packet> out)
{
    if (out->type == obs_encoder_type::OBS_ENCODER_VIDEO) {
        if (!d_ptr->received_video)
            d_ptr->received_video = true;
    } else {
        if (!d_ptr->received_audio)
            d_ptr->received_audio = true;
    }
}

void lite_obs_output::insert_interleaved_packet(std::shared_ptr<encoder_packet> out)
{
    auto iter = d_ptr->interleaved_packets.begin();
    for (; iter != d_ptr->interleaved_packets.end(); iter++) {
        auto cur_packet = *iter;
        if (out->dts_usec == cur_packet->dts_usec && out->type == obs_encoder_type::OBS_ENCODER_VIDEO) {
            break;
        } else if (out->dts_usec < cur_packet->dts_usec) {
            break;
        }
    }

    d_ptr->interleaved_packets.insert(iter, out);
}

void lite_obs_output::set_higher_ts(std::shared_ptr<encoder_packet> packet)
{
    if (packet->type == obs_encoder_type::OBS_ENCODER_VIDEO) {
        if (d_ptr->highest_video_ts < packet->dts_usec)
            d_ptr->highest_video_ts = packet->dts_usec;
    } else {
        if (d_ptr->highest_audio_ts < packet->dts_usec)
            d_ptr->highest_audio_ts = packet->dts_usec;
    }
}

auto lite_obs_output::find_first_packet_type_idx(obs_encoder_type type, size_t audio_idx)
{
    for (auto iter = d_ptr->interleaved_packets.begin(); iter != d_ptr->interleaved_packets.end(); iter++) {
        auto packet = *iter;

        if (packet->type == type) {
            if (type == obs_encoder_type::OBS_ENCODER_AUDIO && packet->track_idx != audio_idx) {
                continue;
            }

            return iter;
        }
    }

    return d_ptr->interleaved_packets.end();
}

auto lite_obs_output::find_last_packet_type_idx(obs_encoder_type type, size_t audio_idx)
{
    for (auto iter = d_ptr->interleaved_packets.rbegin(); iter != d_ptr->interleaved_packets.rend(); iter++) {
        auto packet = *iter;

        if (packet->type == type) {
            if (type == obs_encoder_type::OBS_ENCODER_AUDIO && packet->track_idx != audio_idx) {
                continue;
            }

            return iter;
        }
    }

    return d_ptr->interleaved_packets.rend();
}

std::shared_ptr<encoder_packet> lite_obs_output::find_first_packet_type(obs_encoder_type type, size_t audio_idx)
{
    auto idx = find_first_packet_type_idx(type, audio_idx);
    return (idx != d_ptr->interleaved_packets.end()) ? *idx : nullptr;
}

std::shared_ptr<encoder_packet> lite_obs_output::find_last_packet_type(obs_encoder_type type, size_t audio_idx)
{
    auto idx = find_last_packet_type_idx(type, audio_idx);
    return (idx != d_ptr->interleaved_packets.rend()) ? *idx : nullptr;
}

auto lite_obs_output::prune_premature_packets()
{
    int64_t max_diff = 0;
    int64_t diff = 0;

    auto video_idx = find_first_packet_type_idx(obs_encoder_type::OBS_ENCODER_VIDEO, 0);
    if (video_idx == d_ptr->interleaved_packets.end()) {
        d_ptr->received_video = false;
        return d_ptr->interleaved_packets.end();
    }

    auto max_idx = video_idx;
    auto video = *video_idx;
    auto duration_usec = video->timebase_num * 1000000LL / video->timebase_den;

    size_t audio_mixes = 1;
    for (size_t i = 0; i < audio_mixes; i++) {
        auto audio_idx = find_first_packet_type_idx(obs_encoder_type::OBS_ENCODER_AUDIO, i);
        if (audio_idx == d_ptr->interleaved_packets.end()) {
            d_ptr->received_audio = false;
            return d_ptr->interleaved_packets.end();
        }

        auto audio = *audio_idx;
        auto distance = std::distance(audio_idx, max_idx);
        if (distance < 0)
            max_idx = audio_idx;

        diff = audio->dts_usec - video->dts_usec;
        if (diff > max_diff)
            max_diff = diff;
    }

    return diff > duration_usec ? max_idx++ : d_ptr->interleaved_packets.begin();
}

auto lite_obs_output::get_interleaved_start_idx()
{
    int64_t closest_diff = 0x7FFFFFFFFFFFFFFFLL;
    auto first_video = find_first_packet_type(obs_encoder_type::OBS_ENCODER_VIDEO, 0);
    std::list<std::shared_ptr<encoder_packet>>::iterator video_idx = d_ptr->interleaved_packets.end();
    std::list<std::shared_ptr<encoder_packet>>::iterator idx = d_ptr->interleaved_packets.begin();

    for (auto iter = d_ptr->interleaved_packets.begin(); iter != d_ptr->interleaved_packets.end(); iter++) {
        auto packet = *iter;
        if (packet->type != obs_encoder_type::OBS_ENCODER_AUDIO) {
            if (packet == first_video)
                video_idx = iter;
            continue;
        }

        auto diff = llabs(packet->dts_usec - first_video->dts_usec);
        if (diff < closest_diff) {
            closest_diff = diff;
            idx = iter;
        }
    }

    if (std::distance(video_idx, idx) > 0)
        return video_idx;
    else
        return idx;
}

bool lite_obs_output::prune_interleaved_packets()
{
    std::list<std::shared_ptr<encoder_packet>>::iterator start_idx{};
    auto prune_start = prune_premature_packets();

    /* prunes the first video packet if it's too far away from audio */
    if (prune_start == d_ptr->interleaved_packets.end())
        return false;
    else if (prune_start != d_ptr->interleaved_packets.begin())
        start_idx = prune_start;
    else
        start_idx = get_interleaved_start_idx();

    discard_to_idx(std::distance(d_ptr->interleaved_packets.begin(), start_idx));

    return true;
}

bool lite_obs_output::get_audio_and_video_packets(std::shared_ptr<encoder_packet> &video, std::shared_ptr<encoder_packet> &audio)
{
    video = find_first_packet_type(obs_encoder_type::OBS_ENCODER_VIDEO, 0);
    if (!video)
        d_ptr->received_video = false;

    audio = find_first_packet_type(obs_encoder_type::OBS_ENCODER_AUDIO, 0);
    if (!audio) {
        d_ptr->received_audio = false;
        return false;
    }

    if (!video) {
        return false;
    }

    return true;
}

bool lite_obs_output::initialize_interleaved_packets()
{
    std::shared_ptr<encoder_packet> video{}, audio{}, last_audio{};

    if (!get_audio_and_video_packets(video, audio))
        return false;

    last_audio = find_last_packet_type(obs_encoder_type::OBS_ENCODER_AUDIO, 0);

    if (last_audio->dts_usec < video->dts_usec) {
        d_ptr->received_audio = false;
        return false;
    }

    /* clear out excess starting audio if it hasn't been already */
    auto start_idx = get_interleaved_start_idx();
    if (start_idx != d_ptr->interleaved_packets.end()) {
        discard_to_idx(std::distance(d_ptr->interleaved_packets.begin(), start_idx));
        if (!get_audio_and_video_packets(video, audio))
            return false;
    }

    /* get new offsets */
    d_ptr->video_offset = video->pts;
    d_ptr->audio_offsets[0] = audio->dts;

    /* subtract offsets from highest TS offset variables */
    d_ptr->highest_audio_ts -= audio->dts_usec;
    d_ptr->highest_video_ts -= video->dts_usec;

    /* apply new offsets to all existing packet DTS/PTS values */
    for (auto iter = d_ptr->interleaved_packets.begin(); iter != d_ptr->interleaved_packets.end(); iter++) {
        apply_interleaved_packet_offset(*iter);
    }

    return true;
}

void lite_obs_output::resort_interleaved_packets()
{
    auto old_array = d_ptr->interleaved_packets;
    d_ptr->interleaved_packets.clear();
    for (auto iter = old_array.begin(); iter != old_array.end(); iter++) {
        insert_interleaved_packet(*iter);
    }
    old_array.clear();
}

bool lite_obs_output::has_higher_opposing_ts(std::shared_ptr<encoder_packet> packet)
{
    if (packet->type == obs_encoder_type::OBS_ENCODER_VIDEO)
        return d_ptr->highest_audio_ts > packet->dts_usec;
    else
        return d_ptr->highest_video_ts > packet->dts_usec;
}

void lite_obs_output::send_interleaved()
{
    auto out = d_ptr->interleaved_packets.front();

    /* do not send an interleaved packet if there's no packet of the
         * opposing type of a higher timestamp in the interleave buffer.
         * this ensures that the timestamps are monotonic */
    if (!has_higher_opposing_ts(out))
        return;

    d_ptr->interleaved_packets.pop_front();

    if (out->type == obs_encoder_type::OBS_ENCODER_VIDEO) {
        d_ptr->total_frames++;
    }

    i_encoded_packet(out);
}

void lite_obs_output::interleave_packets_internal(const std::shared_ptr<encoder_packet> &packet)
{
    if (!d_ptr->active)
        return;

    if (packet->type == obs_encoder_type::OBS_ENCODER_AUDIO)
        packet->track_idx = 0;

    std::lock_guard<std::mutex> lock(d_ptr->interleaved_mutex);

    /* if first video frame is not a keyframe, discard until received */
    if (!d_ptr->received_video && packet->type == obs_encoder_type::OBS_ENCODER_VIDEO && !packet->keyframe) {
        discard_unused_audio_packets(packet->dts_usec);
        return;
    }

    auto was_started = d_ptr->received_audio && d_ptr->received_video;
    auto out = std::make_shared<encoder_packet>();
    *out = *packet;
    out->data = std::make_shared<std::vector<uint8_t>>();
    out->data->resize(packet->data->size());
    memcpy(out->data->data(), packet->data->data(), packet->data->size());

    if (was_started)
        apply_interleaved_packet_offset(out);
    else
        check_received(packet);

    insert_interleaved_packet(out);
    set_higher_ts(out);

    /* when both video and audio have been received, we're ready
         * to start sending out packets (one at a time) */
    if (d_ptr->received_audio && d_ptr->received_video) {
        if (!was_started) {
            if (prune_interleaved_packets()) {
                if (initialize_interleaved_packets()) {
                    resort_interleaved_packets();
                    send_interleaved();
                }
            }
        } else {
            send_interleaved();
        }
    }
}

void lite_obs_output::interleave_packets(void *data, const std::shared_ptr<encoder_packet> &packet)
{
    auto output = (lite_obs_output *)data;
    output->interleave_packets_internal(packet);
}

void lite_obs_output::default_encoded_callback_internal(const std::shared_ptr<encoder_packet> &packet)
{
    if (d_ptr->data_active) {
        if (packet->type == obs_encoder_type::OBS_ENCODER_AUDIO)
            packet->track_idx = 0;

        i_encoded_packet(packet);

        if (packet->type == obs_encoder_type::OBS_ENCODER_VIDEO)
            d_ptr->total_frames++;
    }
}

void lite_obs_output::default_encoded_callback(void *param, const std::shared_ptr<encoder_packet> &packet)
{
    auto output = (lite_obs_output *)param;
    output->default_encoded_callback_internal(packet);
}

void lite_obs_output::default_raw_video_callback_internal(struct video_data *frame)
{
    if (d_ptr->data_active)
        i_raw_video(frame);
    d_ptr->total_frames++;
}

void lite_obs_output::default_raw_video_callback(void *param, struct video_data *frame)
{
    auto output = (lite_obs_output *)param;
    output->default_raw_video_callback_internal(frame);
}

bool lite_obs_output::prepare_audio(const struct audio_data *old_data, struct audio_data *new_data)
{
    if (!d_ptr->video_start_ts)
        return false;

    /* ------------------ */

    *new_data = *old_data;

    if (old_data->timestamp < d_ptr->video_start_ts) {
        uint64_t duration = (uint64_t)old_data->frames * 1000000000 / (uint64_t)d_ptr->sample_rate;
        uint64_t end_ts = (old_data->timestamp + duration);
        uint64_t cutoff;

        if (end_ts <= d_ptr->video_start_ts)
            return false;

        cutoff = d_ptr->video_start_ts - old_data->timestamp;
        new_data->timestamp += cutoff;

        cutoff = cutoff * (uint64_t)d_ptr->sample_rate / 1000000000;

        for (size_t i = 0; i < d_ptr->planes; i++)
            new_data->data[i] += d_ptr->audio_size *(uint32_t)cutoff;
        new_data->frames -= (uint32_t)cutoff;
    }

    return true;
}

void lite_obs_output::default_raw_audio_callback_internal(size_t mix_idx, struct audio_data *in)
{
    struct audio_data out;
    size_t frame_size_bytes;

    if (!d_ptr->data_active)
        return;

    /* -------------- */

    if (!prepare_audio(in, &out))
        return;
    if (!d_ptr->audio_start_ts) {
        d_ptr->audio_start_ts = out.timestamp;
    }

    frame_size_bytes = AUDIO_OUTPUT_FRAMES * d_ptr->audio_size;

    for (size_t i = 0; i < d_ptr->planes; i++)
        circlebuf_push_back(&d_ptr->audio_buffer[mix_idx][i],
                            out.data[i],
                            out.frames * d_ptr->audio_size);

    /* -------------- */

    while (d_ptr->audio_buffer[mix_idx][0].size > frame_size_bytes) {
        for (size_t i = 0; i < d_ptr->planes; i++) {
            circlebuf_pop_front(&d_ptr->audio_buffer[mix_idx][i],
                                d_ptr->audio_data[i],
                                frame_size_bytes);
            out.data[i] = (uint8_t *)d_ptr->audio_data[i];
        }

        out.frames = AUDIO_OUTPUT_FRAMES;
        out.timestamp = d_ptr->audio_start_ts +
                audio_frames_to_ns(d_ptr->sample_rate,
                                   d_ptr->total_audio_frames);

        d_ptr->total_audio_frames += AUDIO_OUTPUT_FRAMES;

        i_raw_audio(&out);
    }
}

void lite_obs_output::default_raw_audio_callback(void *param, size_t mix_idx, struct audio_data *in)
{
    auto output = (lite_obs_output *)param;
    output->default_raw_audio_callback_internal(mix_idx, in);
}

void lite_obs_output::hook_data_capture(bool encoded, bool has_video, bool has_audio)
{
    auto core_video = d_ptr->core_video.lock();
    new_packet encoded_callback;

    if (encoded) {
        d_ptr->interleaved_mutex.lock();
        reset_packet_data();
        d_ptr->interleaved_mutex.unlock();

        encoded_callback = (has_video && has_audio) ? lite_obs_output::interleave_packets : lite_obs_output::default_encoded_callback;

        if (has_audio) {
            auto ac = d_ptr->audio_encoders[0].lock();
            if (ac)
                ac->obs_encoder_start(encoded_callback, this);
        }
        if (has_video) {
            auto vo = d_ptr->video.lock();
            if (vo) {
                auto info = vo->video_output_get_info();
                uint32_t fps = info->fps_num / info->fps_den;
                uint32_t target_freq = d_ptr->sei_count_per_second == 0 ? 5 : d_ptr->sei_count_per_second;
                auto vc = d_ptr->video_encoder.lock();
                if (vc) {
                    vc->lite_obs_encoder_set_sei_rate(fps / target_freq);
                    vc->obs_encoder_start(encoded_callback, this);
                }
            }
        }
    } else {
        if (has_video) {
            core_video->lite_obs_core_video_change_raw_active(true);
            auto vo = d_ptr->video.lock();
            if (vo)
                vo->video_output_connect(get_video_conversion(), lite_obs_output::default_raw_video_callback, this);
        }
        if (has_audio) {
            auto ao = d_ptr->audio.lock();
            if (ao)
                ao->audio_output_connect(0, get_audio_conversion(), lite_obs_output::default_raw_audio_callback, this);
        }
    }
}

bool lite_obs_output::lite_obs_output_begin_data_capture()
{
    if (d_ptr->active)
        return false;

    d_ptr->total_frames = 0;

    if (!i_encoded()) {
        reset_raw_output();
    }

    if (!can_begin_data_capture(i_encoded(), i_has_video(), i_has_audio()))
        return false;

    if (i_has_video() && i_has_audio())
        pair_encoders();

    d_ptr->data_active = true;
    hook_data_capture(i_encoded(), i_has_video(), i_has_audio());

    if (d_ptr->signal_callback.activate)
        d_ptr->signal_callback.activate(d_ptr->signal_callback.opaque);

    d_ptr->active = true;

    if (d_ptr->reconnecting) {
        if (d_ptr->signal_callback.reconnect_success)
            d_ptr->signal_callback.reconnect_success(d_ptr->signal_callback.opaque);

        d_ptr->reconnecting = false;
    } else {
        if (d_ptr->signal_callback.start)
            d_ptr->signal_callback.start(d_ptr->signal_callback.opaque);
    }

    return true;
}

void lite_obs_output::log_frame_info()
{
    auto core_video = d_ptr->core_video.lock();
    uint32_t drawn = core_video->total_frames() - d_ptr->starting_drawn_count;
    uint32_t lagged = core_video->lagged_frames() - d_ptr->starting_lagged_count;

    int dropped = i_get_dropped_frames();
    int total = d_ptr->total_frames;

    double percentage_lagged = 0.0f;
    double percentage_dropped = 0.0f;

    if (drawn)
        percentage_lagged = (double)lagged / (double)drawn * 100.0;
    if (dropped)
        percentage_dropped = (double)dropped / (double)total * 100.0;

    blog(LOG_INFO, "Output  stopping");
    if (!dropped || !total)
        blog(LOG_INFO, "Output: Total frames output: %d", total);
    else
        blog(LOG_INFO, "Output : Total frames output: %d (%d attempted)", total - dropped, total);

    if (!lagged || !drawn)
        blog(LOG_INFO, "Output: Total drawn frames: %u", drawn);
    else
        blog(LOG_INFO,
             "Output: Total drawn frames: %u (%u attempted)", drawn - lagged, drawn);

    if (drawn && lagged)
        blog(LOG_INFO, "Output: Number of lagged frames due to rendering lag/stalls: %u (%0.1f%%)", lagged, percentage_lagged);
    if (total && dropped)
        blog(LOG_INFO, "Output '%s': Number of dropped frames due to insufficient bandwidth/connection stalls: %d (%0.1f%%)", dropped, percentage_dropped);
}

void lite_obs_output::end_data_capture_thread_internal()
{
    new_packet encoded_callback;
    if (i_encoded()) {
        encoded_callback = (i_has_video() && i_has_audio()) ? lite_obs_output::interleave_packets : lite_obs_output::default_encoded_callback;

        if (i_has_video()) {
            auto video_encoder = d_ptr->video_encoder.lock();
            if (video_encoder)
                video_encoder->obs_encoder_stop(encoded_callback, this);
        }
        if (i_has_audio()) {
            auto audio_encoder = d_ptr->audio_encoders[0].lock();
            if (audio_encoder) {
                audio_encoder->obs_encoder_stop(encoded_callback,this);
            }
        }
    } else {
        if (i_has_video()) {
            d_ptr->core_video.lock()->lite_obs_core_video_change_raw_active(false);
            auto vo = d_ptr->video.lock();
            if (vo)
                vo->video_output_disconnect(lite_obs_output::default_raw_video_callback, this);
        }
        if (i_has_audio()) {
            auto ao = d_ptr->audio.lock();
            if (ao)
                ao->audio_output_disconnect(0, lite_obs_output::default_raw_audio_callback, this);
        }
    }

    if (d_ptr->signal_callback.deactivate)
        d_ptr->signal_callback.deactivate(d_ptr->signal_callback.opaque);
    d_ptr->active = false;
    os_event_signal(d_ptr->stopping_event);
}

void lite_obs_output::end_data_capture_thread(void *data)
{
    lite_obs_output *output = (lite_obs_output *)data;
    output->end_data_capture_thread_internal();
}

void lite_obs_output::lite_obs_output_end_data_capture_internal(bool sig)
{
    if (!d_ptr->active || !d_ptr->data_active) {
        if (sig) {
            if (d_ptr->signal_callback.stop)
                d_ptr->signal_callback.stop(d_ptr->stop_code, d_ptr->last_error_message.c_str(), d_ptr->signal_callback.opaque);

            d_ptr->stop_code = LITE_OBS_OUTPUT_SUCCESS;
            os_event_signal(d_ptr->stopping_event);
        }
        return;
    }

    d_ptr->data_active = false;

    if (d_ptr->video.lock())
        log_frame_info();

    if (d_ptr->end_data_capture_thread.joinable())
        d_ptr->end_data_capture_thread.join();

    d_ptr->end_data_capture_thread = std::thread(lite_obs_output::end_data_capture_thread, this);

    if (sig) {
        if (d_ptr->signal_callback.stop)
            d_ptr->signal_callback.stop(d_ptr->stop_code, d_ptr->last_error_message.c_str(), d_ptr->signal_callback.opaque);
        d_ptr->stop_code = LITE_OBS_OUTPUT_SUCCESS;
    }
}

void lite_obs_output::lite_obs_output_end_data_capture()
{
    lite_obs_output_end_data_capture_internal(true);
}

void lite_obs_output::lite_obs_output_remove_encoder(std::shared_ptr<lite_obs_encoder> encoder)
{
    if (d_ptr->video_encoder.lock() == encoder)
        d_ptr->video_encoder.reset();
    else {
        if (d_ptr->audio_encoders[0].lock() == encoder)
            d_ptr->audio_encoders[0].reset();
    }
}

void lite_obs_output::lite_obs_output_flush_packet()
{
    d_ptr->interleaved_mutex.lock();
    i_encoded_packet(nullptr);
    d_ptr->interleaved_mutex.unlock();
}

bool lite_obs_output::can_reconnect(int code)
{
    bool reconnect_active = d_ptr->reconnect_retry_max != 0;

    return (d_ptr->reconnecting && code != LITE_OBS_OUTPUT_SUCCESS) || (reconnect_active && code == LITE_OBS_OUTPUT_DISCONNECTED);
}

void lite_obs_output::reconnect_thread(void *param)
{
    auto output = (lite_obs_output *)param;
    unsigned long ms = output->d_ptr->reconnect_retry_cur_sec * 1000;


    if (os_event_timedwait(output->d_ptr->reconnect_stop_event, ms) == ETIMEDOUT) {
        blog(LOG_INFO, "output do reconnect.");
        output->lite_obs_output_actual_start();
    }

    output->d_ptr->reconnecting = false;
    blog(LOG_DEBUG, "output reconnect thread finished.");
}

void lite_obs_output::output_reconnect()
{
    if (!d_ptr->reconnecting) {
        d_ptr->reconnect_retry_cur_sec = d_ptr->reconnect_retry_sec;
        d_ptr->reconnect_retries = 0;
    }

    if (d_ptr->reconnect_retries >= d_ptr->reconnect_retry_max) {
        d_ptr->stop_code = LITE_OBS_OUTPUT_DISCONNECTED;
        d_ptr->reconnecting = false;
        lite_obs_output_end_data_capture();
        return;
    }

    if (!d_ptr->reconnecting) {
        d_ptr->reconnecting = true;
        os_event_reset(d_ptr->reconnect_stop_event);
    }

    d_ptr->reconnect_retries++;

    d_ptr->stop_code = LITE_OBS_OUTPUT_DISCONNECTED;
    d_ptr->reconnect_thread = std::thread(lite_obs_output::reconnect_thread, this);
    blog(LOG_INFO, "Reconnecting in %d seconds..", d_ptr->reconnect_retry_sec);

    if(d_ptr->signal_callback.reconnect)
        d_ptr->signal_callback.reconnect(d_ptr->signal_callback.opaque);
}

void lite_obs_output::lite_obs_output_signal_stop(int code)
{
    d_ptr->stop_code = code;

    if (can_reconnect(code)) {
        lite_obs_output_end_data_capture_internal(false);
        output_reconnect();
    } else {
        lite_obs_output_end_data_capture();
    }
}

void lite_obs_output::lite_obs_output_set_last_error(const std::string &error)
{
    d_ptr->last_error_message = error;
}

const std::string &lite_obs_output::lite_obs_output_last_error()
{
    return d_ptr->last_error_message;
}

