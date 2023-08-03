#include "lite-obs/lite_obs_core_audio.h"
#include "lite-obs/util/circlebuf.h"
#include "lite-obs/util/log.h"
#include "lite-obs/lite_source.h"
#include <list>

struct lite_obs_core_audio_private
{
    std::shared_ptr<audio_output> audio{};

    uint64_t buffered_ts{};
    circlebuf buffered_timestamps{};
    uint64_t buffering_wait_ticks{};
    int total_buffering_ticks{};
};

lite_obs_core_audio::lite_obs_core_audio()
{
    d_ptr = std::make_unique<lite_obs_core_audio_private>();
}

lite_obs_core_audio::~lite_obs_core_audio()
{

}

std::shared_ptr<audio_output> lite_obs_core_audio::core_audio()
{
    return d_ptr->audio;
}

void lite_obs_core_audio::find_min_ts(uint64_t *min_ts)
{
    for (auto iter = lite_source::sources.begin(); iter != lite_source::sources.end(); iter++) {
        auto &pair = *iter;
        if(!(pair.first & source_type::Source_Audio))
            continue;

        auto &source = pair.second;
        if (!source->audio_pending() && source->audio_ts() && source->audio_ts() < *min_ts) {
            *min_ts = source->audio_ts();
        }
    }
}

bool lite_obs_core_audio::mark_invalid_sources(size_t sample_rate, uint64_t min_ts)
{
    bool recalculate = false;

    for (auto iter = lite_source::sources.begin(); iter != lite_source::sources.end(); iter++) {
        auto &pair = *iter;
        if(!(pair.first & source_type::Source_Audio))
            continue;

        auto &source = pair.second;
        recalculate |= source->audio_buffer_insuffient(sample_rate, min_ts);
    }

    return recalculate;
}

void lite_obs_core_audio::calc_min_ts(size_t sample_rate, uint64_t *min_ts)
{
    find_min_ts(min_ts);
    if (mark_invalid_sources(sample_rate, *min_ts))
        find_min_ts(min_ts);
}

void lite_obs_core_audio::add_audio_buffering(size_t sample_rate, struct ts_info *ts, uint64_t min_ts)
{
    struct ts_info new_ts;
    uint64_t offset;
    uint64_t frames;
    size_t total_ms;
    size_t ms;
    int ticks;

    if (d_ptr->total_buffering_ticks == MAX_BUFFERING_TICKS)
        return;

    if (!d_ptr->buffering_wait_ticks)
        d_ptr->buffered_ts = ts->start;

    offset = ts->start - min_ts;
    frames = ns_to_audio_frames(sample_rate, offset);
    ticks = (int)((frames + AUDIO_OUTPUT_FRAMES - 1) / AUDIO_OUTPUT_FRAMES);

    d_ptr->total_buffering_ticks += ticks;

    if (d_ptr->total_buffering_ticks >= MAX_BUFFERING_TICKS) {
        ticks -= d_ptr->total_buffering_ticks - MAX_BUFFERING_TICKS;
        d_ptr->total_buffering_ticks = MAX_BUFFERING_TICKS;
        blog(LOG_WARNING, "Max audio buffering reached!");
    }

    ms = ticks * AUDIO_OUTPUT_FRAMES * 1000 / sample_rate;
    total_ms = d_ptr->total_buffering_ticks * AUDIO_OUTPUT_FRAMES * 1000 / sample_rate;

    blog(LOG_INFO,
         "adding %d milliseconds of audio buffering, total "
         "audio buffering is now %d milliseconds"
         " (source)\n",
         (int)ms, (int)total_ms);

    new_ts.start = d_ptr->buffered_ts - audio_frames_to_ns(sample_rate, d_ptr->buffering_wait_ticks * AUDIO_OUTPUT_FRAMES);

    while (ticks--) {
        uint64_t cur_ticks = ++d_ptr->buffering_wait_ticks;

        new_ts.end = new_ts.start;
        new_ts.start = d_ptr->buffered_ts - audio_frames_to_ns(sample_rate, cur_ticks * AUDIO_OUTPUT_FRAMES);

        circlebuf_push_front(&d_ptr->buffered_timestamps, &new_ts, sizeof(new_ts));
    }

    *ts = new_ts;
}

bool lite_obs_core_audio::audio_callback_internal(uint64_t start_ts_in, uint64_t end_ts_in, uint64_t *out_ts, uint32_t mixers, audio_output_data *mixes)
{
    size_t sample_rate = d_ptr->audio->audio_output_get_sample_rate();
    size_t channels = d_ptr->audio->audio_output_get_channels();
    ts_info ts = {start_ts_in, end_ts_in};
    size_t audio_size;
    uint64_t min_ts;

    circlebuf_push_back(&d_ptr->buffered_timestamps, &ts, sizeof(ts));
    circlebuf_peek_front(&d_ptr->buffered_timestamps, &ts, sizeof(ts));
    min_ts = ts.start;

    audio_size = AUDIO_OUTPUT_FRAMES * sizeof(float);

    std::list<std::shared_ptr<lite_source>> audio_sources;
    lite_source::sources_mutex.lock();
    for (auto iter = lite_source::sources.begin(); iter != lite_source::sources.end(); iter++) {
        auto &pair = *iter;
        if (pair.first & source_type::Source_Audio) {
            audio_sources.push_back(pair.second);
        }
    }
    lite_source::sources_mutex.unlock();

    /* ------------------------------------------------ */
    /* render audio data */
    for (auto iter = audio_sources.begin(); iter != audio_sources.end(); iter++) {
        auto &source = *iter;
        source->audio_render(mixers, channels, sample_rate, audio_size);
    }

    /* ------------------------------------------------ */
    /* get minimum audio timestamp */
    lite_source::sources_mutex.lock();
    calc_min_ts(sample_rate, &min_ts);
    lite_source::sources_mutex.unlock();

    /* ------------------------------------------------ */
    /* if a source has gone backward in time, buffer */
    if (min_ts < ts.start)
        add_audio_buffering(sample_rate, &ts, min_ts);

    /* ------------------------------------------------ */
    /* mix audio */
    if (!d_ptr->buffering_wait_ticks) {

        for (auto iter = audio_sources.begin(); iter != audio_sources.end(); iter++) {

            auto &source = *iter;
            if (source->audio_pending())
                continue;

            source->mix_audio(mixes, channels, sample_rate, &ts);
        }
    }

    /* ------------------------------------------------ */
    /* discard audio */
    lite_source::sources_mutex.lock();
    for (auto iter = lite_source::sources.begin(); iter != lite_source::sources.end(); iter++) {
        auto &pair = *iter;
        if(!(pair.first & source_type::Source_Audio))
            continue;

        auto &source = pair.second;
        source->discard_audio(d_ptr->total_buffering_ticks, channels, sample_rate, &ts);
    }
    lite_source::sources_mutex.unlock();

    /* ------------------------------------------------ */
    /* release audio sources */
    audio_sources.clear();

    circlebuf_pop_front(&d_ptr->buffered_timestamps, NULL, sizeof(ts));

    *out_ts = ts.start;

    if (d_ptr->buffering_wait_ticks) {
        d_ptr->buffering_wait_ticks--;
        return false;
    }

    return true;
}


bool lite_obs_core_audio::audio_callback(void *param, uint64_t start_ts_in, uint64_t end_ts_in,
                                         uint64_t *out_ts, uint32_t mixers,
                                         audio_output_data *mixes)
{
    auto core_audio = (lite_obs_core_audio *)param;
    return core_audio->audio_callback_internal(start_ts_in, end_ts_in, out_ts, mixers, mixes);
}

bool lite_obs_core_audio::lite_obs_start_audio(lite_obs::output_audio_info *oai)
{
    if (d_ptr->audio && d_ptr->audio->audio_output_active())
        return false;

    free_audio();

    audio_output_info ai{};
    ai.name = "Audio";
    ai.samples_per_sec = oai->samples_per_sec;
    ai.format = audio_format::AUDIO_FORMAT_FLOAT_PLANAR;
    ai.speakers = oai->speakers;
    ai.input_callback = lite_obs_core_audio::audio_callback;
    ai.input_param = this;

    blog(LOG_INFO, "---------------------------------");
    blog(LOG_INFO,
         "audio settings reset:\n"
         "\tsamples per sec: %d\n"
         "\tspeakers:        %d",
         (int)ai.samples_per_sec, (int)ai.speakers);

    auto audio = std::make_shared<audio_output>();
    if (audio->audio_output_open(&ai) != AUDIO_OUTPUT_SUCCESS)
        return false;

    d_ptr->audio = audio;
    return true;
}

void lite_obs_core_audio::lite_obs_stop_audio()
{
    if (d_ptr->audio) {
        d_ptr->audio->audio_output_close();
        d_ptr->audio.reset();
    }

    free_audio();
}

void lite_obs_core_audio::free_audio()
{
    if (d_ptr->audio)
        d_ptr->audio->audio_output_close();

    circlebuf_free(&d_ptr->buffered_timestamps);
    d_ptr->buffered_ts = 0;
    d_ptr->buffering_wait_ticks = 0;
    d_ptr->total_buffering_ticks = 0;
}
