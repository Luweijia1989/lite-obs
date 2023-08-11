#include "lite-obs/media-io/audio_output.h"

#include "lite-obs/util/threading.h"
#include "lite-obs/util/log.h"

#include "lite-obs/media-io/audio_resampler.h"

#include <thread>
#include <mutex>
#include <vector>
#include <memory>

class audio_input {
public:
    audio_input(){}
    ~audio_input() {}

    bool audio_input_init(audio_output_info *output_info) {
        if (conversion.format != output_info->format ||
                conversion.samples_per_sec != output_info->samples_per_sec ||
                conversion.speakers != output_info->speakers) {
            resample_info from = {
                output_info->samples_per_sec,
                output_info->format,
                output_info->speakers};

            resample_info to = {
                conversion.samples_per_sec,
                conversion.format,
                conversion.speakers};

            resampler = std::make_unique<audio_resampler>();
            if (!resampler->create(&to, &from)) {
                resampler.reset();
                blog(LOG_ERROR, "audio_input_init: Failed to "
                                "create resampler");
                return false;
            }
        }

        return true;
    }

public:
    audio_convert_info conversion{};
    std::unique_ptr<audio_resampler> resampler{};

    audio_output_callback_t callback{};
    void *param{};
};

struct audio_mix {
    std::vector<std::shared_ptr<audio_input>> inputs;
    float buffer[MAX_AUDIO_CHANNELS][AUDIO_OUTPUT_FRAMES]{};
};

struct audio_output_private {
    audio_output_info info{};
    size_t block_size{};
    size_t channels{};
    size_t planes{};

    std::thread thread;
    os_event_t *stop_event{};

    bool initialized{};

    audio_input_callback_t input_cb{};
    void *input_param{};
    std::recursive_mutex input_mutex;
    audio_mix mixes[MAX_AUDIO_MIXES]{};
};

audio_output::audio_output()
{

}

audio_output::~audio_output()
{

}

static inline bool valid_audio_params(const audio_output_info *info)
{
    return info->format != audio_format::AUDIO_FORMAT_UNKNOWN && info->name && info->samples_per_sec > 0 &&
            info->speakers != speaker_layout::SPEAKERS_UNKNOWN;
}

void audio_output::clamp_audio_output(size_t bytes)
{
    size_t float_size = bytes / sizeof(float);

    for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
        audio_mix *mix = &d_ptr->mixes[mix_idx];

        /* do not process mixing if a specific mix is inactive */
        if (!mix->inputs.size())
            continue;

        for (size_t plane = 0; plane < d_ptr->planes; plane++) {
            float *mix_data = mix->buffer[plane];
            float *mix_end = &mix_data[float_size];

            while (mix_data < mix_end) {
                float val = *mix_data;
                val = (val > 1.0f) ? 1.0f : val;
                val = (val < -1.0f) ? -1.0f : val;
                *(mix_data++) = val;
            }
        }
    }
}

bool audio_output::resample_audio_output(std::shared_ptr<audio_input> input, audio_data *data)
{
    bool success = true;

    if (input->resampler) {
        uint8_t *output[MAX_AV_PLANES];
        uint32_t frames;
        uint64_t offset;

        memset(output, 0, sizeof(output));

        success = input->resampler->do_resample(output, &frames, &offset, (const uint8_t *const *)data->data, data->frames);

        for (size_t i = 0; i < MAX_AV_PLANES; i++)
            data->data[i] = output[i];
        data->frames = frames;
        data->timestamp -= offset;
    }

    return success;
}

void audio_output::do_audio_output(size_t mix_idx, uint64_t timestamp, uint32_t frames)
{
    audio_mix *mix = &d_ptr->mixes[mix_idx];
    audio_data data;

    std::lock_guard<std::recursive_mutex> lock(d_ptr->input_mutex);

    for (size_t i = mix->inputs.size(); i > 0; i--) {
        auto input = mix->inputs.at(i - 1);

        for (size_t j = 0; j < d_ptr->planes; j++)
            data.data[j] = (uint8_t *)mix->buffer[j];
        data.frames = frames;
        data.timestamp = timestamp;


        if (resample_audio_output(input, &data))
            input->callback(input->param, mix_idx, &data);
    }
}

void audio_output::input_and_output(uint64_t audio_time, uint64_t prev_time)
{
    size_t bytes = AUDIO_OUTPUT_FRAMES * d_ptr->block_size;
    audio_output_data data[MAX_AUDIO_MIXES];
    uint32_t active_mixes = 0;
    uint64_t new_ts = 0;
    bool success;

    memset(data, 0, sizeof(data));

#ifdef DEBUG_AUDIO
    blog(LOG_DEBUG, "audio_time: %llu, prev_time: %llu, bytes: %lu",
         audio_time, prev_time, bytes);
#endif

    /* get mixers */
    d_ptr->input_mutex.lock();
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        if (d_ptr->mixes[i].inputs.size() > 0)
            active_mixes |= (1 << i);
    }
    d_ptr->input_mutex.unlock();

    /* clear mix buffers */
    for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
        audio_mix *mix = &d_ptr->mixes[mix_idx];

        memset(mix->buffer[0], 0,
                AUDIO_OUTPUT_FRAMES * MAX_AUDIO_CHANNELS *
                sizeof(float));

        for (size_t i = 0; i < d_ptr->planes; i++)
            data[mix_idx].data[i] = mix->buffer[i];
    }

    /* get new audio data */
    if (!d_ptr->input_cb)
        return;

    success = d_ptr->input_cb(d_ptr->input_param, prev_time, audio_time, &new_ts, active_mixes, data);
    if (!success)
        return;

    /* clamps audio data to -1.0..1.0 */
    clamp_audio_output(bytes);

    /* output */
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++)
        do_audio_output(i, new_ts, AUDIO_OUTPUT_FRAMES);
}

void audio_output::audio_thread_internal()
{
    size_t rate = d_ptr->info.samples_per_sec;
    uint64_t samples = 0;
    uint64_t start_time = os_gettime_ns();
    uint64_t prev_time = start_time;
    uint64_t audio_time = prev_time;
    uint32_t audio_wait_time = (uint32_t)(
                audio_frames_to_ns(rate, AUDIO_OUTPUT_FRAMES) / 1000000);


    while (os_event_try(d_ptr->stop_event) == EAGAIN) {
        uint64_t cur_time;

        os_sleep_ms(audio_wait_time);

        cur_time = os_gettime_ns();
        while (audio_time <= cur_time) {
            samples += AUDIO_OUTPUT_FRAMES;
            audio_time =
                    start_time + audio_frames_to_ns(rate, samples);

            input_and_output(audio_time, prev_time);
            prev_time = audio_time;
        }
    }
}

void audio_output::audio_thread(void *param)
{
    audio_output *audio = (audio_output *)param;
    audio->audio_thread_internal();
}

int audio_output::audio_output_open(audio_output_info *info)
{
    bool planar = is_audio_planar(info->format);
    if (!valid_audio_params(info))
        return AUDIO_OUTPUT_INVALIDPARAM;

    d_ptr = std::make_unique<audio_output_private>();
    if (!d_ptr)
        goto fail;

    memcpy(&d_ptr->info, info, sizeof(audio_output_info));
    d_ptr->channels = get_audio_channels(info->speakers);
    d_ptr->planes = planar ? d_ptr->channels : 1;
    d_ptr->input_cb = info->input_callback;
    d_ptr->input_param = info->input_param;
    d_ptr->block_size = (planar ? 1 : d_ptr->channels) *
            get_audio_bytes_per_channel(info->format);

    if (os_event_init(&d_ptr->stop_event, OS_EVENT_TYPE_MANUAL) != 0)
        goto fail;

    d_ptr->thread = std::thread(audio_output::audio_thread, this);

    d_ptr->initialized = true;
    return AUDIO_OUTPUT_SUCCESS;

fail:
    audio_output_close();
    return AUDIO_OUTPUT_FAIL;
}

void audio_output::audio_output_close()
{
    if (!d_ptr)
        return;

    if (d_ptr->initialized) {
        os_event_signal(d_ptr->stop_event);
        if (d_ptr->thread.joinable())
            d_ptr->thread.join();
    }

    for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
        audio_mix *mix = &d_ptr->mixes[mix_idx];
        mix->inputs.clear();
    }

    os_event_destroy(d_ptr->stop_event);
    d_ptr.reset();
}

int audio_output::get_input_index(size_t mix_idx, audio_output_callback_t callback, void *param)
{
    const audio_mix *mix = &d_ptr->mixes[mix_idx];
    for (size_t i = 0; i < mix->inputs.size(); i++) {
        auto input = mix->inputs.at(i);
        if (input->callback == callback && input->param == param)
            return i;
    }

    return -1;
}

bool audio_output::audio_output_connect(size_t mix_idx,
                                        const audio_convert_info *conversion,
                                        audio_output_callback_t callback, void *param)
{
    bool success = false;

    if (!d_ptr || mix_idx >= MAX_AUDIO_MIXES)
        return false;

    std::lock_guard<std::recursive_mutex> lock(d_ptr->input_mutex);
    if (get_input_index(mix_idx, callback, param) == -1) {
        audio_mix *mix = &d_ptr->mixes[mix_idx];
        auto input = std::make_shared<audio_input>();
        input->callback = callback;
        input->param = param;

        if (conversion) {
            input->conversion = *conversion;
        } else {
            input->conversion.format = d_ptr->info.format;
            input->conversion.speakers = d_ptr->info.speakers;
            input->conversion.samples_per_sec = d_ptr->info.samples_per_sec;
        }

        if (input->conversion.format == audio_format::AUDIO_FORMAT_UNKNOWN)
            input->conversion.format = d_ptr->info.format;
        if (input->conversion.speakers == speaker_layout::SPEAKERS_UNKNOWN)
            input->conversion.speakers = d_ptr->info.speakers;
        if (input->conversion.samples_per_sec == 0)
            input->conversion.samples_per_sec = d_ptr->info.samples_per_sec;

        success = input->audio_input_init(&d_ptr->info);
        if (success)
            mix->inputs.push_back(std::move(input));
    }

    return success;
}

void audio_output::audio_output_disconnect(size_t mix_idx,
                                           audio_output_callback_t callback,
                                           void *param)
{
    if (!d_ptr || mix_idx > MAX_AUDIO_MIXES)
        return;

    std::lock_guard<std::recursive_mutex> lock(d_ptr->input_mutex);
    auto idx = get_input_index(mix_idx, callback, param);
    if (idx != -1) {
        auto mix = &d_ptr->mixes[mix_idx];
        mix->inputs.erase(mix->inputs.begin() + idx);
    }
}

bool audio_output::audio_output_active()
{
    if (!d_ptr)
        return false;

    for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
        const audio_mix *mix = &d_ptr->mixes[mix_idx];

        if (mix->inputs.size() != 0)
            return true;
    }

    return false;
}

size_t audio_output::audio_output_get_block_size()
{
    return d_ptr ? d_ptr->block_size : 0;
}

size_t audio_output::audio_output_get_planes()
{
    return d_ptr ? d_ptr->planes : 0;
}

size_t audio_output::audio_output_get_channels()
{
    return d_ptr ? d_ptr->channels : 0;
}

uint32_t audio_output::audio_output_get_sample_rate()
{
    return d_ptr ? d_ptr->info.samples_per_sec : 0;
}

const audio_output_info *audio_output::audio_output_get_info()
{
    return d_ptr ? &d_ptr->info : nullptr;
}
