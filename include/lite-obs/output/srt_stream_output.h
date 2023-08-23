#pragma once

#include "lite-obs/lite_obs_output.h"

struct mpeg_ts_output_private;
struct ffmpeg_data;
struct ffmpeg_cfg;
class mpeg_ts_output : public lite_obs_output
{
public:
    mpeg_ts_output();

    virtual void i_set_output_info(const std::string &info) override;
    virtual bool i_output_valid() override;
    virtual bool i_has_video() override;
    virtual bool i_has_audio() override;
    virtual bool i_encoded() override;
    virtual bool i_create() override;
    virtual void i_destroy() override;
    virtual bool i_start() override;
    virtual void i_stop(uint64_t ts) override;
    virtual void i_raw_video(struct video_data *frame) override;
    virtual void i_raw_audio(struct audio_data *frames) override;
    virtual void i_encoded_packet(std::shared_ptr<struct encoder_packet> packet) override;
    virtual uint64_t i_get_total_bytes() override;
    virtual int i_get_dropped_frames() override;
    virtual std::string i_cdn_ip() override;

    static void start_thread(void *data);
    static void write_thread(void *data);

private:
    void ffmpeg_mpegts_log_error(int log_level, struct ffmpeg_data *data, const char *format, ...);

    bool ffmpeg_mpegts_data_init(ffmpeg_data *data, ffmpeg_cfg *config);
    void ffmpeg_mpegts_data_free(ffmpeg_data **d);

    bool new_stream(ffmpeg_data *data, struct AVStream **stream, const char *name);
    bool create_video_stream(ffmpeg_data *data);
    bool create_audio_stream(ffmpeg_data *data, int idx);
    bool init_streams(ffmpeg_data *data);
    int allocate_custom_aviocontext();
    int open_output_file(ffmpeg_data *data);
    bool set_config();

    void close_video(ffmpeg_data *data);
    void close_audio(ffmpeg_data *data);

    int connect_mpegts_url();
    void close_mpegts_url();

    void start_internal();
    void full_stop();

    bool get_video_headers(ffmpeg_data *data);
    bool get_audio_headers(ffmpeg_data *data, int idx);
    bool get_extradata();
    uint64_t get_packet_sys_dts(struct AVPacket *packet);
    bool write_header(ffmpeg_data *data);
    int mpegts_process_packet();
    void mpegts_write_packet(std::shared_ptr<struct encoder_packet> encpacket);
    void write_internal();

    void ffmpeg_mpegts_deactivate();

private:
    std::shared_ptr<mpeg_ts_output_private> d_ptr{};
};
