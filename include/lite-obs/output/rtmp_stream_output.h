#pragma once

#include "lite-obs/lite_obs_output.h"

struct rtmp_stream_output_private;
class rtmp_stream_output : public lite_obs_output
{
public:
    rtmp_stream_output();
    virtual ~rtmp_stream_output();

    virtual void i_set_output_info(void *info) override;
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
    virtual void i_encoded_packet(std::shared_ptr<encoder_packet> packet) override;
    virtual uint64_t i_get_total_bytes() override;
    virtual int i_get_dropped_frames() override;

private:
    bool send_meta_data();
    bool send_headers();
    bool send_audio_header();
    bool send_video_header();

    void dbr_add_frame(std::shared_ptr<struct dbr_frame> frame);
    void dbr_set_bitrate();
    void dbr_inc_bitrate();
    bool dbr_bitrate_lowered();

    std::shared_ptr<encoder_packet> find_first_video_packet();
    void check_to_drop_frames(bool pframes);
    void drop_frames(const char *name, int highest_priority, bool pframes);
    bool add_video_packet(const std::shared_ptr<encoder_packet> &packet);
    bool add_packet(const std::shared_ptr<encoder_packet> &packet);


    bool discard_recv_data(size_t size);
    int send_packet(const std::shared_ptr<encoder_packet> &packet, bool is_header);

private:
#ifdef _WIN32
    void win32_log_interface_type();
#endif
    void adjust_sndbuf_size(int new_size);
    void set_output_error();

    bool init_connect();
    int init_send();
    int try_connect();
    static void connect_thread(void *param);
    void connect_thread_internal();

    std::shared_ptr<encoder_packet> get_next_packet();
    bool can_shutdown_stream(const std::shared_ptr<encoder_packet> &packet);
    static void send_thread(void *param);
    void send_thread_internal();

private:
    void free_packets();

private:
    std::unique_ptr<rtmp_stream_output_private> d_ptr{};
};
