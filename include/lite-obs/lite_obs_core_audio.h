#pragma once

#include <memory>
#include "lite_obs_defines.h"

#define MAX_BUFFERING_TICKS 45

struct ts_info {
    uint64_t start;
    uint64_t end;
};

struct lite_obs_core_audio_private;
class audio_output;
class lite_obs_core_audio
{
public:
    struct output_audio_info {
        uint32_t samples_per_sec{};
        speaker_layout speakers{};
    };

    lite_obs_core_audio(uintptr_t core_ptr);
    ~lite_obs_core_audio();

    std::shared_ptr<audio_output> core_audio();

    bool lite_obs_start_audio(uint32_t sample_rate);
    void lite_obs_stop_audio();

private:
    bool audio_callback_internal(uint64_t start_ts_in, uint64_t end_ts_in, uint64_t *out_ts, uint32_t mixers, struct audio_output_data *mixes);
    static bool audio_callback(void *param, uint64_t start_ts_in, uint64_t end_ts_in, uint64_t *out_ts, uint32_t mixers, struct audio_output_data *mixes);

    void find_min_ts(uint64_t *min_ts);
    bool mark_invalid_sources(size_t sample_rate, uint64_t min_ts);
    void calc_min_ts(size_t sample_rate, uint64_t *min_ts);
    void add_audio_buffering(size_t sample_rate, struct ts_info *ts, uint64_t min_ts);

private:
    void free_audio();

private:
    std::unique_ptr<lite_obs_core_audio_private> d_ptr{};
};
