#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include "audio_info.h"
#include <memory>

struct audio_data {
    uint8_t *data[MAX_AV_PLANES]{};
    uint32_t frames{};
    uint64_t timestamp{};
};

struct audio_output_data {
    float *data[MAX_AUDIO_CHANNELS] = {nullptr};
};

typedef bool (*audio_input_callback_t)(void *param, uint64_t start_ts,
                                       uint64_t end_ts, uint64_t *new_ts,
                                       uint32_t active_mixers,
                                       audio_output_data *mixes);

typedef void (*audio_output_callback_t)(void *param, size_t mix_idx, audio_data *data);

struct audio_output_info {
    const char *name{};

    uint32_t samples_per_sec{};
    audio_format format = audio_format::AUDIO_FORMAT_UNKNOWN;
    speaker_layout speakers = speaker_layout::SPEAKERS_UNKNOWN;

    audio_input_callback_t input_callback{};
    void *input_param{};
};

struct audio_convert_info {
    uint32_t samples_per_sec{};
    audio_format format = audio_format::AUDIO_FORMAT_UNKNOWN;
    speaker_layout speakers = speaker_layout::SPEAKERS_UNKNOWN;
};

#define AUDIO_OUTPUT_SUCCESS 0
#define AUDIO_OUTPUT_INVALIDPARAM -1
#define AUDIO_OUTPUT_FAIL -2

class audio_input;
struct audio_output_private;
class audio_output
{
public:
    audio_output();
    ~audio_output();

    static void audio_thread(void *param);

    int audio_output_open(audio_output_info *info);
    void audio_output_close();

    bool audio_output_connect(size_t mix_idx,
                              const audio_convert_info *conversion,
                              audio_output_callback_t callback, void *param);

    void audio_output_disconnect(size_t mix_idx,
                                 audio_output_callback_t callback,
                                 void *param);

    bool audio_output_active();

    size_t audio_output_get_block_size();
    size_t audio_output_get_planes();
    size_t audio_output_get_channels();
    uint32_t audio_output_get_sample_rate();
    const audio_output_info *audio_output_get_info();

private:
    int get_input_index(size_t mix_idx, audio_output_callback_t callback, void *param);
    void input_and_output(uint64_t audio_time, uint64_t prev_time);
    void clamp_audio_output(size_t bytes);
    void do_audio_output(size_t mix_idx, uint64_t timestamp, uint32_t frames);
    bool resample_audio_output(std::shared_ptr<audio_input> input, audio_data *data);

private:
    void audio_thread_internal();

private:
    std::unique_ptr<audio_output_private> d_ptr{};
};

#endif // AUDIO_OUTPUT_H
