#ifndef AUDIO_RESAMPLER_H
#define AUDIO_RESAMPLER_H

#include "audio_info.h"
#include <memory>

struct resample_info {
    uint32_t samples_per_sec{};
    audio_format format = audio_format::AUDIO_FORMAT_UNKNOWN;
    speaker_layout speakers = speaker_layout::SPEAKERS_UNKNOWN;
};

struct audio_resampler_private;
class audio_resampler
{
public:
    audio_resampler();
    ~audio_resampler();

    bool create(const struct resample_info *dst,
                const struct resample_info *src);
    bool do_resample(uint8_t *output[], uint32_t *out_frames,
                     uint64_t *ts_offset,
                     const uint8_t *const input[],
                     uint32_t in_frames);

private:
    std::unique_ptr<audio_resampler_private> d_ptr{};
};

#endif // AUDIO_RESAMPLER_H
