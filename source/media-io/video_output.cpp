#include "lite-obs/media-io/video_output.h"
#include "lite-obs/media-io/video_scaler.h"
#include "lite-obs/media-io/video_frame.h"
#include "lite-obs/util/threading.h"
#include "lite-obs/util/log.h"
#include <thread>
#include <mutex>
#include <vector>

#define MAX_CONVERT_BUFFERS 3
#define MAX_CACHE_SIZE 16

struct cached_frame_info {
    struct video_data frame{};
    int skipped{};
    int count{};
};

struct video_input
{
    struct video_scale_info conversion{};
    std::unique_ptr<video_scaler> scaler{};
    video_frame frame[MAX_CONVERT_BUFFERS]{};
    int cur_frame{};

    void (*callback)(void *param, struct video_data *frame){};
    void *param{};
};

struct video_output_private
{
    video_output_info info{};

    std::thread thread;
    std::recursive_mutex data_mutex;
    bool stop{};

    os_sem_t *update_semaphore{};
    uint64_t frame_time{};
    volatile std::atomic_long skipped_frames{};
    volatile std::atomic_long total_frames{};

    bool initialized{};

    std::recursive_mutex input_mutex;
    std::vector<std::shared_ptr<video_input>> inputs{};

    size_t available_frames{};
    size_t first_added{};
    size_t last_added{};
    cached_frame_info cache[MAX_CACHE_SIZE]{};

    volatile std::atomic_bool raw_active{};
    volatile std::atomic_long gpu_refs{};
};

video_output::video_output()
{
    d_ptr = std::make_unique<video_output_private>();
}

video_output::~video_output()
{
}

void video_output::video_thread(void *arg)
{
    video_output *out = (video_output *)arg;
    out->video_thread_internal();
}

static inline bool valid_video_params(const struct video_output_info *info)
{
    return info->height != 0 && info->width != 0 && info->fps_den != 0 &&
           info->fps_num != 0;
}

int video_output::video_output_open(video_output_info *info)
{
    if (!valid_video_params(info))
        return VIDEO_OUTPUT_INVALIDPARAM;

    memcpy(&d_ptr->info, info, sizeof(struct video_output_info));
    d_ptr->frame_time = (uint64_t)(1000000000.0 * (double)info->fps_den / (double)info->fps_num);
    d_ptr->initialized = false;

    os_sem_init(&d_ptr->update_semaphore, 0);

    d_ptr->thread = std::thread(video_output::video_thread, this);

    init_cache();

    d_ptr->initialized = true;
    return VIDEO_OUTPUT_SUCCESS;
}

void video_output::video_output_close()
{
    video_output_stop();

    d_ptr->inputs.clear();

    for (size_t i = 0; i < d_ptr->info.cache_size; i++) {
        auto frame = &d_ptr->cache[i].frame;
        frame->frame.frame_free();
    }

    os_sem_destroy(d_ptr->update_semaphore);
}

uint32_t video_output::video_output_get_width()
{
    return d_ptr->info.width;
}

uint32_t video_output::video_output_get_height()
{
    return d_ptr->info.height;
}

double video_output::video_output_get_frame_rate()
{
    return (double)d_ptr->info.fps_num / (double)d_ptr->info.fps_den;
}

video_output_info *video_output::video_output_get_info()
{
    return &d_ptr->info;
}

bool video_output::video_output_connect(const video_scale_info *conversion, void (*callback)(void *, video_data *), void *param)
{
    bool success = false;

    if (!callback)
        return false;

    std::lock_guard<std::recursive_mutex> lock(d_ptr->input_mutex);

    if (video_get_input_idx(callback, param) == -1) {
        auto input = std::make_shared<video_input>();

        input->callback = callback;
        input->param = param;

        if (conversion) {
            input->conversion = *conversion;
        } else {
            input->conversion.format = d_ptr->info.format;
            input->conversion.width = d_ptr->info.width;
            input->conversion.height = d_ptr->info.height;
        }

        if (input->conversion.width == 0)
            input->conversion.width = d_ptr->info.width;
        if (input->conversion.height == 0)
            input->conversion.height = d_ptr->info.height;

        success = video_input_init(input);
        if (success) {
            if (d_ptr->inputs.size() == 0) {
                if (!d_ptr->gpu_refs) {
                    reset_frames();
                }
                d_ptr->raw_active = true;
            }
            d_ptr->inputs.push_back(std::move(input));
        }
    }

    return success;
}

void video_output::video_output_disconnect(void (*callback)(void *, video_data *), void *param)
{
    if (!callback)
        return;

    std::lock_guard<std::recursive_mutex> lock(d_ptr->input_mutex);

    auto idx = video_get_input_idx(callback, param);
    if (idx != -1) {
        d_ptr->inputs.erase(d_ptr->inputs.begin() + idx);

        if (d_ptr->inputs.size() == 0) {
            d_ptr->raw_active = false;
            if (!d_ptr->gpu_refs) {
                log_skipped();
            }
        }
    }
}

void video_output::video_output_stop()
{
    if (d_ptr->initialized) {
        d_ptr->initialized = false;
        d_ptr->stop = true;
        os_sem_post(d_ptr->update_semaphore);
        if (d_ptr->thread.joinable())
            d_ptr->thread.join();
    }
}

bool video_output::video_output_stopped()
{
    return d_ptr->stop;
}

bool video_output::video_output_lock_frame(video_frame *frame, int count, uint64_t timestamp)
{
    bool locked = false;

    std::lock_guard<std::recursive_mutex> lock(d_ptr->data_mutex);

    if (d_ptr->available_frames == 0) {
        d_ptr->cache[d_ptr->last_added].count += count;
        d_ptr->cache[d_ptr->last_added].skipped += count;
        locked = false;

    } else {
        if (d_ptr->available_frames != d_ptr->info.cache_size) {
            if (++d_ptr->last_added == d_ptr->info.cache_size)
                d_ptr->last_added = 0;
        }

        auto cfi = &d_ptr->cache[d_ptr->last_added];
        cfi->frame.timestamp = timestamp;
        cfi->count = count;
        cfi->skipped = 0;

        *frame = cfi->frame.frame;

        locked = true;
    }

    return locked;
}

void video_output::video_output_unlock_frame()
{
    std::lock_guard<std::recursive_mutex> lock(d_ptr->data_mutex);

    d_ptr->available_frames--;
    os_sem_post(d_ptr->update_semaphore);
}

uint64_t video_output::video_output_get_frame_time()
{
    return d_ptr->frame_time;
}

uint32_t video_output::video_output_get_total_frames()
{
    return (uint32_t)d_ptr->total_frames;
}

void video_output::video_thread_internal()
{
    while (os_sem_wait(d_ptr->update_semaphore) == 0) {
        if (d_ptr->stop)
            break;

        while (!d_ptr->stop && !video_output_cur_frame()) {
            d_ptr->total_frames++;
        }

        d_ptr->total_frames++;
    }
}

bool video_output::video_output_cur_frame()
{
    bool complete;
    bool skipped;

    /* -------------------------------- */

    d_ptr->data_mutex.lock();

    auto frame_info = &d_ptr->cache[d_ptr->first_added];

    d_ptr->data_mutex.unlock();

    /* -------------------------------- */

    d_ptr->input_mutex.lock();

    for (size_t i = 0; i < d_ptr->inputs.size(); i++) {
        auto input = d_ptr->inputs[i];
        auto frame = frame_info->frame;

        if (scale_video_output(input, &frame))
            input->callback(input->param, &frame);
    }

    d_ptr->input_mutex.unlock();

    /* -------------------------------- */

    d_ptr->data_mutex.lock();

    frame_info->frame.timestamp += d_ptr->frame_time;
    complete = --frame_info->count == 0;
    skipped = frame_info->skipped > 0;

    if (complete) {
        if (++d_ptr->first_added == d_ptr->info.cache_size)
            d_ptr->first_added = 0;

        if (++d_ptr->available_frames == d_ptr->info.cache_size)
            d_ptr->last_added = d_ptr->first_added;
    } else if (skipped) {
        --frame_info->skipped;
        d_ptr->skipped_frames++;
    }

    d_ptr->data_mutex.unlock();

    /* -------------------------------- */

    return complete;
}

void video_output::init_cache()
{
    if (d_ptr->info.cache_size > MAX_CACHE_SIZE)
        d_ptr->info.cache_size = MAX_CACHE_SIZE;

    for (size_t i = 0; i < d_ptr->info.cache_size; i++) {
        auto frame = &d_ptr->cache[i].frame;
        frame->frame.frame_init(d_ptr->info.format, d_ptr->info.width, d_ptr->info.height);
    }

    d_ptr->available_frames = d_ptr->info.cache_size;
}

int video_output::video_get_input_idx(void (*callback)(void *, video_data *), void *param)
{
    for (size_t i = 0; i < d_ptr->inputs.size(); i++) {
        auto input = d_ptr->inputs[i];
        if (input->callback == callback && input->param == param)
            return (int)i;
    }

    return -1;
}

bool video_output::video_input_init(std::shared_ptr<video_input> input)
{
    if (input->conversion.width != d_ptr->info.width ||
        input->conversion.height != d_ptr->info.height ||
        input->conversion.format != d_ptr->info.format) {
        struct video_scale_info from = {
                                        .format = d_ptr->info.format,
                                        .width = d_ptr->info.width,
                                        .height = d_ptr->info.height,
                                        .range = d_ptr->info.range,
                                        .colorspace = d_ptr->info.colorspace};

        input->scaler = std::make_unique<video_scaler>();
        int ret = input->scaler->create(&input->conversion, &from, video_scale_type::VIDEO_SCALE_FAST_BILINEAR);
        if (ret != VIDEO_SCALER_SUCCESS) {
            if (ret == VIDEO_SCALER_BAD_CONVERSION)
                blog(LOG_ERROR, "video_input_init: Bad "
                                "scale conversion type");
            else
                blog(LOG_ERROR, "video_input_init: Failed to "
                                "create scaler");

            return false;
        }

        for (size_t i = 0; i < MAX_CONVERT_BUFFERS; i++)
            input->frame[i].frame_init(input->conversion.format, input->conversion.width, input->conversion.height);
    }

    return true;
}

void video_output::reset_frames()
{
    d_ptr->skipped_frames = 0;
    d_ptr->total_frames = 0;
}

void video_output::log_skipped()
{
    long skipped = d_ptr->skipped_frames;
    double percentage_skipped =
        (double)skipped /
        (double)d_ptr->total_frames * 100.0;

    if (skipped)
        blog(LOG_INFO,
             "Video stopped, number of "
             "skipped frames due "
             "to encoding lag: "
             "%ld/%ld (%0.1f%%)",
             d_ptr->skipped_frames, d_ptr->total_frames,
             percentage_skipped);
}

bool video_output::scale_video_output(std::shared_ptr<video_input> input, video_data *data)
{
    bool success = true;

    if (input->scaler) {
        video_frame *frame;

        if (++input->cur_frame == MAX_CONVERT_BUFFERS)
            input->cur_frame = 0;

        frame = &input->frame[input->cur_frame];

        success = input->scaler->video_scaler_scale(frame->data.data(), frame->linesize.data(), (const uint8_t *const *)data->frame.data.data(), data->frame.linesize.data());

        if (success) {
            for (size_t i = 0; i < MAX_AV_PLANES; i++) {
                data->frame.data[i] = frame->data[i];
                data->frame.linesize[i] = frame->linesize[i];
            }
        } else {
            blog(LOG_WARNING, "video-io: Could not scale frame!");
        }
    }

    return success;
}

void video_output::video_output_inc_texture_encoders()
{
    d_ptr->gpu_refs++;
    if (d_ptr->gpu_refs == 1 && !d_ptr->raw_active) {
        reset_frames();
    }
}

void video_output::video_output_dec_texture_encoders()
{
    d_ptr->gpu_refs--;
    if (d_ptr->gpu_refs == 0 && !d_ptr->raw_active) {
        log_skipped();
    }
}

void video_output::video_output_inc_texture_frames()
{
    d_ptr->total_frames++;
}

void video_output::video_output_inc_texture_skipped_frames()
{
    d_ptr->skipped_frames++;
}
