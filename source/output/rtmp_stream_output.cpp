#include "lite-obs/output/rtmp_stream_output.h"
#include "lite-obs/util/threading.h"
#include "lite-obs/util/log.h"
#include "lite-obs/lite_encoder.h"
#include "lite-obs/lite_obs_internal.h"
#include "lite-obs/media-io/audio_output.h"
#include "lite-obs/media-io/video_output.h"
#include "lite-obs/lite_obs_avc.h"

#include <mutex>
#include <atomic>
#include <list>
#include <thread>

#include "lite-obs/output/flv_mux.h"

extern "C"
{
#include "librtmp/rtmp.h"
#include "librtmp/log.h"
}

#ifndef SEC_TO_NSEC
#define SEC_TO_NSEC 1000000000ULL
#endif

#ifndef MSEC_TO_USEC
#define MSEC_TO_USEC 1000ULL
#endif

#ifndef MSEC_TO_NSEC
#define MSEC_TO_NSEC 1000000ULL
#endif

/* dynamic bitrate coefficients */
#define DBR_INC_TIMER (30ULL * SEC_TO_NSEC)
#define DBR_TRIGGER_USEC (200ULL * MSEC_TO_USEC)
#define MIN_ESTIMATE_DURATION_MS 1000
#define MAX_ESTIMATE_DURATION_MS 2000

struct dbr_frame {
    uint64_t send_beg{};
    uint64_t send_end{};
    size_t size{};
};

struct rtmp_stream_output_private
{
    std::string stream_url;
    std::string cdn_ip{};
    bool initilized{};

    std::mutex packets_mutex;
    std::list<std::shared_ptr<encoder_packet>> packets;
    bool sent_headers{};
    bool sent_first_media_packet{};

    bool got_first_video{};
    int64_t start_dts_offset{};

    std::atomic_bool connecting{};
    std::thread connect_thread;

    std::atomic_bool active{};
    std::atomic_bool disconnected{};
    std::atomic_bool encode_error{};
    std::thread send_thread;

    int max_shutdown_time_sec{};

    os_sem_t *send_sem{};
    os_event_t *stop_event{};
    uint64_t stop_ts{};
    uint64_t shutdown_timeout_ts{};

    std::string path, key;
    std::string username, password;
    std::string encoder_name;
    std::string bind_ip;

    /* frame drop variables */
    int64_t drop_threshold_usec{};
    int64_t pframe_drop_threshold_usec{};
    int min_priority{};
    float congestion{};

    int64_t last_dts_usec{};

    uint64_t total_bytes_sent{};
    int dropped_frames{};

    std::mutex dbr_mutex;
    std::list<std::shared_ptr<dbr_frame>> dbr_frames;
    size_t dbr_data_size{};
    uint64_t dbr_inc_timeout{};
    long audio_bitrate{};
    long dbr_est_bitrate{};
    long dbr_orig_bitrate{};
    long dbr_prev_bitrate{};
    long dbr_cur_bitrate{};
    long dbr_inc_bitrate{};
    bool dbr_enabled{};

    RTMP rtmp{};

    bool stopping() {
        return os_event_try(stop_event) != EAGAIN;
    }
};

static void log_rtmp(int level, const char *format, va_list args)
{
    if (level > RTMP_LOGWARNING)
        return;

    char out[4096];
    vsnprintf(out, sizeof(out), format, args);

    blog(LOG_INFO, out);
}

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#endif

bool netif_str_to_addr(struct sockaddr_storage *out, int *addr_len,
                       const char *addr)
{
    bool ipv6;

    memset(out, 0, sizeof(*out));
    *addr_len = 0;

    if (!addr)
        return false;

    ipv6 = (strchr(addr, ':') != NULL);
    out->ss_family = ipv6 ? AF_INET6 : AF_INET;
    *addr_len = sizeof(*out);

#ifdef _WIN32
    int ret = WSAStringToAddressA((LPSTR)addr, out->ss_family, NULL,
                                  (LPSOCKADDR)out, addr_len);
    if (ret == SOCKET_ERROR)
        blog(LOG_WARNING, "Could not parse address, error code: %d", GetLastError());
    return ret != SOCKET_ERROR;
#else
    struct sockaddr_in *sin = (struct sockaddr_in *)out;
    if (inet_pton(out->ss_family, addr, &sin->sin_addr)) {
        *addr_len = ipv6 ? sizeof(struct sockaddr_in6)
                         : sizeof(struct sockaddr_in);
        return true;
    }

    return false;
#endif
}

rtmp_stream_output::rtmp_stream_output()
{
#ifdef _WIN32
    WSADATA wsad;
    WSAStartup(MAKEWORD(2, 2), &wsad);
#endif

    d_ptr = std::make_unique<rtmp_stream_output_private>();
}

rtmp_stream_output::~rtmp_stream_output()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

void rtmp_stream_output::i_set_output_info(const std::string &info)
{
    d_ptr->stream_url = info;
}

bool rtmp_stream_output::i_output_valid()
{
    return d_ptr->initilized;
}

bool rtmp_stream_output::i_has_video()
{
    return true;
}

bool rtmp_stream_output::i_has_audio()
{
    return true;
}

bool rtmp_stream_output::i_encoded()
{
    return true;
}

bool rtmp_stream_output::i_create()
{
    RTMP_LogSetCallback(log_rtmp);
    RTMP_Init(&d_ptr->rtmp);
    RTMP_LogSetLevel(RTMP_LOGWARNING);

    os_event_init(&d_ptr->stop_event, OS_EVENT_TYPE_MANUAL);

    d_ptr->initilized = true;
    return true;
}

void rtmp_stream_output::i_destroy()
{
    if (d_ptr->connecting || d_ptr->active) {
        if (d_ptr->connecting) {
            if (d_ptr->connect_thread.joinable())
                d_ptr->connect_thread.join();
        }

        d_ptr->stop_ts = 0;
        os_event_signal(d_ptr->stop_event);

        if (d_ptr->active) {
            os_sem_post(d_ptr->send_sem);
            lite_obs_output_end_data_capture();
        }
    }

    if (d_ptr->send_thread.joinable())
        d_ptr->send_thread.join();

    RTMP_TLS_Free(&d_ptr->rtmp);
    free_packets();
    os_event_destroy(d_ptr->stop_event);
    os_sem_destroy(d_ptr->send_sem);

    d_ptr->dbr_frames.clear();
}

void rtmp_stream_output::connect_thread(void *param)
{
    auto rtmp = (rtmp_stream_output *)param;
    rtmp->connect_thread_internal();
}

bool rtmp_stream_output::init_connect()
{
    if (d_ptr->stopping()) {
        if (d_ptr->send_thread.joinable())
            d_ptr->send_thread.join();
    }

    free_packets();

    d_ptr->disconnected = false;
    d_ptr->encode_error = false;
    d_ptr->total_bytes_sent = 0;
    d_ptr->dropped_frames = 0;
    d_ptr->min_priority = 0;
    d_ptr->got_first_video = false;
    d_ptr->path = d_ptr->stream_url;

    auto drop_b = 700;
    auto drop_p = 900;
    d_ptr->max_shutdown_time_sec = 30;

    auto venc = lite_obs_output_get_video_encoder();
    auto aenc = lite_obs_output_get_audio_encoder(0);

    d_ptr->dbr_frames.clear();
    d_ptr->audio_bitrate = aenc ? aenc->lite_obs_encoder_bitrate() : 0;
    d_ptr->dbr_data_size = 0;
    d_ptr->dbr_orig_bitrate = venc ? venc->lite_obs_encoder_bitrate() : 0;
    d_ptr->dbr_cur_bitrate = d_ptr->dbr_orig_bitrate;
    d_ptr->dbr_est_bitrate = 0;
    d_ptr->dbr_inc_bitrate = d_ptr->dbr_orig_bitrate / 10;
    d_ptr->dbr_inc_timeout = 0;
    d_ptr->dbr_enabled = false;

    if (d_ptr->dbr_enabled) {
        blog(LOG_INFO, "Dynamic bitrate enabled.  Dropped frames begone!");
    }

    if (drop_p < (drop_b + 200))
        drop_p = drop_b + 200;

    d_ptr->drop_threshold_usec = 1000 * drop_b;
    d_ptr->pframe_drop_threshold_usec = 1000 * drop_p;

    return true;
}

#ifdef _WIN32
#include <Iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")
void rtmp_stream_output::win32_log_interface_type()
{
    RTMP *rtmp = &d_ptr->rtmp;
    MIB_IPFORWARDROW route;
    uint32_t dest_addr, source_addr;
    char hostname[256];
    HOSTENT *h;

    if (rtmp->Link.hostname.av_len >= sizeof(hostname) - 1)
        return;

    strncpy(hostname, rtmp->Link.hostname.av_val, sizeof(hostname));
    hostname[rtmp->Link.hostname.av_len] = 0;

    h = gethostbyname(hostname);
    if (!h)
        return;

    dest_addr = *(uint32_t *)h->h_addr_list[0];

    if (rtmp->m_bindIP.addrLen == 0)
        source_addr = 0;
    else if (rtmp->m_bindIP.addr.ss_family == AF_INET)
        source_addr = (*(struct sockaddr_in *)&rtmp->m_bindIP.addr)
                          .sin_addr.S_un.S_addr;
    else
        return;

    if (!GetBestRoute(dest_addr, source_addr, &route)) {
        MIB_IFROW row;
        memset(&row, 0, sizeof(row));
        row.dwIndex = route.dwForwardIfIndex;

        if (!GetIfEntry(&row)) {
            uint32_t speed = row.dwSpeed / 1000000;
            const char *type;

            if (row.dwType == IF_TYPE_ETHERNET_CSMACD) {
                type = "ethernet";
            } else if (row.dwType == IF_TYPE_IEEE80211) {
                type = "802.11";
            } else {
                type = "unknown type";
            }

            blog(LOG_INFO, "Interface: %s (%s, %lu mbps)", row.bDescr, type, speed);
        }
    }
}
#endif

void rtmp_stream_output::set_output_error()
{
    const char *msg = NULL;
#ifdef _WIN32
    switch (d_ptr->rtmp.last_error_code) {
    case WSAETIMEDOUT:
        msg = "ConnectionTimedOut";
        break;
    case WSAEACCES:
        msg = "PermissionDenied";
        break;
    case WSAECONNABORTED:
        msg = "ConnectionAborted";
        break;
    case WSAECONNRESET:
        msg = "ConnectionReset";
        break;
    case WSAHOST_NOT_FOUND:
        msg = "HostNotFound";
        break;
    case WSANO_DATA:
        msg = "NoData";
        break;
    case WSAEADDRNOTAVAIL:
        msg = "AddressNotAvailable";
        break;
    }
#else
    switch (d_ptr->rtmp.last_error_code) {
    case ETIMEDOUT:
        msg = "ConnectionTimedOut";
        break;
    case EACCES:
        msg = "PermissionDenied";
        break;
    case ECONNABORTED:
        msg = "ConnectionAborted";
        break;
    case ECONNRESET:
        msg = "ConnectionReset";
        break;
    case HOST_NOT_FOUND:
        msg = "HostNotFound";
        break;
    case NO_DATA:
        msg = "NoData";
        break;
    case EADDRNOTAVAIL:
        msg = "AddressNotAvailable";
        break;
    }
#endif

    // non platform-specific errors
    if (!msg) {
        switch (d_ptr->rtmp.last_error_code) {
        case -0x2700:
            msg = "SSLCertVerifyFailed";
            break;
        case -0x7680:
            msg = "Failed to load root certificates for a secure TLS connection."
#if defined(__linux__)
                  " Check you have an up to date root certificate bundle in /etc/ssl/certs."
#endif
                ;
            break;
        }
    }

    if (msg)
        lite_obs_output_set_last_error(msg);
}

#ifdef _WIN32
#define socklen_t int
#endif

#define MIN_SENDBUF_SIZE 65535

void rtmp_stream_output::adjust_sndbuf_size(int new_size)
{
    int cur_sendbuf_size = new_size;
    socklen_t int_size = sizeof(int);

    getsockopt(d_ptr->rtmp.m_sb.sb_socket, SOL_SOCKET, SO_SNDBUF, (char *)&cur_sendbuf_size, &int_size);

    if (cur_sendbuf_size < new_size) {
        cur_sendbuf_size = new_size;
        setsockopt(d_ptr->rtmp.m_sb.sb_socket, SOL_SOCKET, SO_SNDBUF, (const char *)&cur_sendbuf_size, int_size);
    }
}

bool rtmp_stream_output::send_meta_data()
{
    auto venc = lite_obs_output_get_video_encoder();
    auto aenc = lite_obs_output_get_audio_encoder(0);

    auto audio = lite_obs_output_audio();
    auto video = lite_obs_output_video();

    std::vector<uint8_t> meta_data;
    flv_meta_data(lite_obs_output_get_width(), lite_obs_output_get_height(),
                  venc->lite_obs_encoder_bitrate(), video->video_output_get_frame_rate(),
                  (int)audio->audio_output_get_channels(), audio->audio_output_get_sample_rate(),
                  aenc->lite_obs_encoder_bitrate(), meta_data, false);
    auto success = RTMP_Write(&d_ptr->rtmp, (char *)meta_data.data(), (int)meta_data.size(), 0) >= 0;

    return success;
}

std::shared_ptr<encoder_packet> rtmp_stream_output::get_next_packet()
{
    std::lock_guard<std::mutex> lock(d_ptr->packets_mutex);
    if (!d_ptr->packets.empty()) {
        auto packet = d_ptr->packets.front();
        d_ptr->packets.pop_front();
        return packet;
    }

    return nullptr;
}

bool rtmp_stream_output::can_shutdown_stream(const std::shared_ptr<encoder_packet> &packet)
{
    uint64_t cur_time = os_gettime_ns();
    bool timeout = cur_time >= d_ptr->shutdown_timeout_ts;

    if (timeout)
        blog(LOG_INFO, "Stream shutdown timeout reached (%d second(s))", d_ptr->max_shutdown_time_sec);

    return timeout || packet->sys_dts_usec >= (int64_t)d_ptr->stop_ts;
}

bool rtmp_stream_output::send_audio_header()
{
    auto aencoder = lite_obs_output_get_audio_encoder(0);
    if (!aencoder)
        return false;

    auto packet = std::make_shared<encoder_packet>();
    packet->type = obs_encoder_type::OBS_ENCODER_AUDIO;
    packet->timebase_den = 1;

    uint8_t *header = nullptr;
    size_t size = 0;
    aencoder->lite_obs_encoder_get_extra_data(&header, &size);
    packet->data = std::make_shared<std::vector<uint8_t>>();
    packet->data->resize(size);
    memcpy(packet->data->data(), header, size);
    return send_packet(packet, true) >= 0;
}

bool rtmp_stream_output::send_video_header()
{
    auto vencoder = lite_obs_output_get_video_encoder();
    if (!vencoder)
        return false;

    auto packet = std::make_shared<encoder_packet>();
    packet->type = obs_encoder_type::OBS_ENCODER_VIDEO;
    packet->timebase_den = 1;
    packet->keyframe = true;

    uint8_t *header = nullptr;
    size_t size = 0;
    vencoder->lite_obs_encoder_get_extra_data(&header, &size);

    packet->data = std::make_shared<std::vector<uint8_t>>();
    obs_parse_avc_header(*packet->data, header, size);
    return send_packet(packet, true) >= 0;
}

bool rtmp_stream_output::send_headers()
{
    blog(LOG_INFO, "rtmp_stream_output send headers");

    d_ptr->sent_headers = true;

    if (!send_audio_header())
        return false;
    if (!send_video_header())
        return false;

    return true;
}

bool rtmp_stream_output::discard_recv_data(size_t size)
{
    RTMP *rtmp = &d_ptr->rtmp;
    uint8_t buf[512];
#ifdef _WIN32
    int ret;
#else
    ssize_t ret;
#endif

    do {
        size_t bytes = size > 512 ? 512 : size;
        size -= bytes;

#ifdef _WIN32
        ret = recv(rtmp->m_sb.sb_socket, (char *)buf, (int)bytes, 0);
#else
        ret = recv(rtmp->m_sb.sb_socket, buf, bytes, 0);
#endif

        if (ret <= 0) {
#ifdef _WIN32
            int error = WSAGetLastError();
#else
            int error = errno;
#endif
            if (ret < 0) {
                blog(LOG_ERROR, "recv error: %d (%d bytes)", error, (int)size);
            }
            return false;
        }
    } while (size > 0);

    return true;
}

int rtmp_stream_output::send_packet(const std::shared_ptr<encoder_packet> &packet, bool is_header)
{
    int recv_size = 0;

#ifdef _WIN32
    auto ret = ioctlsocket(d_ptr->rtmp.m_sb.sb_socket, FIONREAD, (u_long *)&recv_size);
#else
    auto ret = ioctl(d_ptr->rtmp.m_sb.sb_socket, FIONREAD, &recv_size);
#endif

    if (ret >= 0 && recv_size > 0) {
        if (!discard_recv_data((size_t)recv_size))
            return -1;
    }

    std::vector<uint8_t> data;
    flv_packet_mux(packet, is_header ? 0 : (int32_t)d_ptr->start_dts_offset, data, is_header);

    ret = RTMP_Write(&d_ptr->rtmp, (char *)data.data(), (int)data.size(), 0);

    if (!d_ptr->sent_first_media_packet) {
        d_ptr->sent_first_media_packet = true;
        auto callback = output_signal_callback();
        if (callback.first_media_packet)
            callback.first_media_packet(callback.opaque);
    }

    d_ptr->total_bytes_sent += data.size();
    return ret;
}

void rtmp_stream_output::dbr_add_frame(std::shared_ptr<dbr_frame> back)
{
    d_ptr->dbr_frames.push_back(back);
    auto front = d_ptr->dbr_frames.front();

    d_ptr->dbr_data_size += back->size;

    auto dur = (back->send_end - front->send_beg) / 1000000;

    if (dur >= MAX_ESTIMATE_DURATION_MS) {
        d_ptr->dbr_data_size -= front->size;
        d_ptr->dbr_frames.pop_front();
    }

    d_ptr->dbr_est_bitrate = (dur >= MIN_ESTIMATE_DURATION_MS) ? (long)(d_ptr->dbr_data_size * 1000 / dur) : 0;
    d_ptr->dbr_est_bitrate *= 8;
    d_ptr->dbr_est_bitrate /= 1000;

    if (d_ptr->dbr_est_bitrate) {
        d_ptr->dbr_est_bitrate -= d_ptr->audio_bitrate;
        if (d_ptr->dbr_est_bitrate < 50)
            d_ptr->dbr_est_bitrate = 50;
    }
}

void rtmp_stream_output::dbr_set_bitrate()
{
    auto vencoder = lite_obs_output_get_video_encoder();
    if (!vencoder)
        return;

    vencoder->lite_obs_encoder_update_bitrate((int)d_ptr->dbr_cur_bitrate);
}

void rtmp_stream_output::send_thread_internal()
{
    while (os_sem_wait(d_ptr->send_sem) == 0) {
        if (d_ptr->stopping() && d_ptr->stop_ts == 0) {
            break;
        }

        auto packet = get_next_packet();
        if (!packet)
            continue;

        if (d_ptr->stopping()) {
            if (can_shutdown_stream(packet)) {
                packet.reset();
                break;
            }
        }

        if (!d_ptr->sent_headers || (packet->type == obs_encoder_type::OBS_ENCODER_VIDEO && packet->encoder_first_packet)) {
            if (!send_headers()) {
                d_ptr->disconnected = true;
                break;
            }
        }

        auto frame = std::make_shared<dbr_frame>();
        if (d_ptr->dbr_enabled) {
            frame->send_beg = os_gettime_ns();
            frame->size = packet->data->size();
        }

        if (send_packet(packet, false) < 0) {
            d_ptr->disconnected = true;
            break;
        }

        if (d_ptr->dbr_enabled) {
            frame->send_end = os_gettime_ns();

            std::lock_guard<std::mutex> lock(d_ptr->dbr_mutex);
            dbr_add_frame(frame);
        }
    }

    bool encode_error = d_ptr->encode_error;

    if (d_ptr->disconnected) {
        blog(LOG_INFO, "Disconnected from %s", d_ptr->path.c_str());
    } else if (encode_error) {
        blog(LOG_INFO, "Encoder error, disconnecting");
    } else {
        blog(LOG_INFO, "User stopped the stream");
    }

    set_output_error();
    RTMP_Close(&d_ptr->rtmp);

    if (!d_ptr->stopping()) {
        d_ptr->send_thread.detach();
        lite_obs_output_signal_stop(LITE_OBS_OUTPUT_DISCONNECTED);
    } else if (encode_error) {
        lite_obs_output_signal_stop(LITE_OBS_OUTPUT_ENCODE_ERROR);
    } else {
        lite_obs_output_end_data_capture();
    }

    free_packets();
    os_event_reset(d_ptr->stop_event);
    d_ptr->active = false;
    d_ptr->sent_headers = false;
    d_ptr->sent_first_media_packet = false;

    /* reset bitrate on stop */
    if (d_ptr->dbr_enabled) {
        if (d_ptr->dbr_cur_bitrate != d_ptr->dbr_orig_bitrate) {
            d_ptr->dbr_cur_bitrate = d_ptr->dbr_orig_bitrate;
            dbr_set_bitrate();
        }
    }
}

void rtmp_stream_output::send_thread(void *param)
{
    auto rtmp = (rtmp_stream_output *)param;
    rtmp->send_thread_internal();
}

int rtmp_stream_output::init_send()
{
#if defined(_WIN32)
    adjust_sndbuf_size(MIN_SENDBUF_SIZE);
#endif

    os_sem_destroy(d_ptr->send_sem);
    os_sem_init(&d_ptr->send_sem, 0);

    if (d_ptr->send_thread.joinable())
        d_ptr->send_thread.join();

    d_ptr->send_thread = std::thread(rtmp_stream_output::send_thread, this);

    d_ptr->active = true;

    if (!send_meta_data()) {
        blog(LOG_WARNING, "Disconnected while attempting to send metadata");
        set_output_error();
        return LITE_OBS_OUTPUT_DISCONNECTED;
    }

    lite_obs_output_begin_data_capture();

    return LITE_OBS_OUTPUT_SUCCESS;
}

int rtmp_stream_output::try_connect()
{
    if (d_ptr->path.empty()) {
        blog(LOG_WARNING, "URL is empty");
        return LITE_OBS_OUTPUT_BAD_PATH;
    }

    blog(LOG_DEBUG, "Connecting to RTMP URL %s...", d_ptr->path.c_str());

    // on reconnect we need to reset the internal variables of librtmp
    // otherwise the data sent/received will not parse correctly on the other end
    RTMP_Reset(&d_ptr->rtmp);

    // since we don't call RTMP_Init above, there's no other good place
    // to reset this as doing it in RTMP_Close breaks the ugly RTMP
    // authentication system
    memset(&d_ptr->rtmp.Link, 0, sizeof(d_ptr->rtmp.Link));
    d_ptr->rtmp.last_error_code = 0;

    if (!RTMP_SetupURL(&d_ptr->rtmp, (char *)d_ptr->path.c_str()))
        return LITE_OBS_OUTPUT_BAD_PATH;

    RTMP_EnableWrite(&d_ptr->rtmp);

    d_ptr->encoder_name = "FMLE/3.0 (compatible; FMSc/1.0)";

    d_ptr->rtmp.Link.pubUser.av_val = nullptr;
    d_ptr->rtmp.Link.pubUser.av_len = 0;
    d_ptr->rtmp.Link.pubPasswd.av_val = nullptr;
    d_ptr->rtmp.Link.pubPasswd.av_len = 0;
    d_ptr->rtmp.Link.flashVer.av_val = (char *)d_ptr->encoder_name.c_str();
    d_ptr->rtmp.Link.flashVer.av_len = (int)d_ptr->encoder_name.length();
    d_ptr->rtmp.Link.swfUrl = d_ptr->rtmp.Link.tcUrl;

    if (d_ptr->bind_ip.empty() || d_ptr->bind_ip == "default") {
        memset(&d_ptr->rtmp.m_bindIP, 0, sizeof(d_ptr->rtmp.m_bindIP));
    } else {
        bool success = netif_str_to_addr(&d_ptr->rtmp.m_bindIP.addr,
                                         &d_ptr->rtmp.m_bindIP.addrLen,
                                         d_ptr->bind_ip.c_str());
        if (success) {
            int len = d_ptr->rtmp.m_bindIP.addrLen;
            bool ipv6 = len == sizeof(struct sockaddr_in6);
            blog(LOG_INFO, "Binding to IPv%d", ipv6 ? 6 : 4);
        }
    }

    RTMP_AddStream(&d_ptr->rtmp, d_ptr->key.c_str());

    d_ptr->rtmp.m_outChunkSize = 4096;
    d_ptr->rtmp.m_bSendChunkSizeInfo = true;
    d_ptr->rtmp.m_bUseNagle = true;

#ifdef _WIN32
    win32_log_interface_type();
#endif

    if (!RTMP_Connect(&d_ptr->rtmp, NULL)) {
        set_output_error();
        return LITE_OBS_OUTPUT_CONNECT_FAILED;
    }

    if (!RTMP_ConnectStream(&d_ptr->rtmp, 0))
        return LITE_OBS_OUTPUT_INVALID_STREAM;

    blog(LOG_INFO, "Connection to %s successful", d_ptr->path.c_str());

    return init_send();
}

void rtmp_stream_output::connect_thread_internal()
{
    if (!init_connect()) {
        lite_obs_output_signal_stop(LITE_OBS_OUTPUT_BAD_PATH);
        return;
    }

    auto ret = try_connect();

    if (ret != LITE_OBS_OUTPUT_SUCCESS) {
        lite_obs_output_signal_stop(ret);
        blog(LOG_DEBUG, "Connection to %s failed: %d", d_ptr->path.c_str(), ret);
    } else {
        auto callback = output_signal_callback();
        if (callback.connected)
            callback.connected(callback.opaque);
    }

    d_ptr->cdn_ip = d_ptr->rtmp.cdn_addr;

    if (!d_ptr->stopping())
        d_ptr->connect_thread.detach();

    d_ptr->connecting = false;
}

bool rtmp_stream_output::i_start()
{
    if (!lite_obs_output_can_begin_data_capture())
        return false;
    if (!lite_obs_output_initialize_encoders())
        return false;

    d_ptr->connecting = true;

    d_ptr->connect_thread = std::thread(rtmp_stream_output::connect_thread, this);
    return true;
}

void rtmp_stream_output::i_stop(uint64_t ts)
{
    if (d_ptr->stopping() && ts != 0)
        return;

    if (d_ptr->connecting) {
        if (d_ptr->connect_thread.joinable())
            d_ptr->connect_thread.join();
    }

    d_ptr->stop_ts = ts / 1000ULL;

    if (ts)
        d_ptr->shutdown_timeout_ts = ts + (uint64_t)d_ptr->max_shutdown_time_sec * 1000000000ULL;

    if (d_ptr->active) {
        os_event_signal(d_ptr->stop_event);
        if (d_ptr->stop_ts == 0)
            os_sem_post(d_ptr->send_sem);
    } else {
        lite_obs_output_signal_stop(LITE_OBS_OUTPUT_SUCCESS);
    }
}

void rtmp_stream_output::i_raw_video(video_data *frame)
{
}

void rtmp_stream_output::i_raw_audio(audio_data *frames)
{
}

bool rtmp_stream_output::add_packet(const std::shared_ptr<encoder_packet> &packet)
{
    d_ptr->packets.push_back(packet);
    return true;
}

void rtmp_stream_output::dbr_inc_bitrate()
{
    d_ptr->dbr_prev_bitrate = d_ptr->dbr_cur_bitrate;
    d_ptr->dbr_cur_bitrate += d_ptr->dbr_inc_bitrate;

    if (d_ptr->dbr_cur_bitrate >= d_ptr->dbr_orig_bitrate) {
        d_ptr->dbr_cur_bitrate = d_ptr->dbr_orig_bitrate;
        blog(LOG_INFO, "bitrate increased to: %ld, done", d_ptr->dbr_cur_bitrate);
    } else if (d_ptr->dbr_cur_bitrate < d_ptr->dbr_orig_bitrate) {
        d_ptr->dbr_inc_timeout = os_gettime_ns() + DBR_INC_TIMER;
        blog(LOG_INFO, "bitrate increased to: %ld, waiting", d_ptr->dbr_cur_bitrate);
    }
}

bool rtmp_stream_output::dbr_bitrate_lowered()
{
    long prev_bitrate = d_ptr->dbr_prev_bitrate;
    long est_bitrate = 0;
    long new_bitrate;

    if (d_ptr->dbr_est_bitrate &&
        d_ptr->dbr_est_bitrate < d_ptr->dbr_cur_bitrate) {
        d_ptr->dbr_data_size = 0;
        d_ptr->dbr_frames.pop_front();
        est_bitrate = d_ptr->dbr_est_bitrate / 100 * 100;
        if (est_bitrate < 50) {
            est_bitrate = 50;
        }
    }

    if (est_bitrate) {
        new_bitrate = est_bitrate;

    } else if (prev_bitrate) {
        new_bitrate = prev_bitrate;
        blog(LOG_INFO, "going back to prev bitrate");

    } else {
        return false;
    }

    if (new_bitrate == d_ptr->dbr_cur_bitrate) {
        return false;
    }

    d_ptr->dbr_prev_bitrate = 0;
    d_ptr->dbr_cur_bitrate = new_bitrate;
    d_ptr->dbr_inc_timeout = os_gettime_ns() + DBR_INC_TIMER;
    blog(LOG_INFO, "bitrate decreased to: %ld", d_ptr->dbr_cur_bitrate);
    return true;
}

std::shared_ptr<encoder_packet> rtmp_stream_output::find_first_video_packet()
{
    for (auto iter = d_ptr->packets.begin(); iter != d_ptr->packets.end(); iter++) {
        auto cur = *iter;
        if (cur->type == obs_encoder_type::OBS_ENCODER_VIDEO && !cur->keyframe)
            return cur;
    }

    return nullptr;
}

void rtmp_stream_output::drop_frames(const char *name, int highest_priority, bool pframes)
{
#ifdef _DEBUG
    int start_packets = (int)d_ptr->packets.size();
#else
    (void)name;
#endif

    std::list<std::shared_ptr<encoder_packet>> new_buf;
    new_buf.resize(8);

    int num_frames_dropped = 0;
    while (d_ptr->packets.size()) {
        auto packet = d_ptr->packets.front();
        d_ptr->packets.pop_front();

        /* do not drop audio data or video keyframes */
        if (packet->type == obs_encoder_type::OBS_ENCODER_AUDIO ||
            packet->drop_priority >= highest_priority) {
            new_buf.push_back(packet);
        } else {
            num_frames_dropped++;
        }
    }

    d_ptr->packets = new_buf;

    if (d_ptr->min_priority < highest_priority)
        d_ptr->min_priority = highest_priority;
    if (!num_frames_dropped)
        return;

    d_ptr->dropped_frames += num_frames_dropped;
#ifdef _DEBUG
    blog(LOG_DEBUG, "Dropped %s, prev packet count: %d, new packet count: %d", name, start_packets, d_ptr->packets.size());
#endif
}

void rtmp_stream_output::check_to_drop_frames(bool pframes)
{
    size_t num_packets = d_ptr->packets.size();
    const char *name = pframes ? "p-frames" : "b-frames";
    int priority = pframes ? OBS_NAL_PRIORITY_HIGHEST
                           : OBS_NAL_PRIORITY_HIGH;
    int64_t drop_threshold = pframes ? d_ptr->pframe_drop_threshold_usec
                                     : d_ptr->drop_threshold_usec;

    if (!pframes && d_ptr->dbr_enabled) {
        if (d_ptr->dbr_inc_timeout) {
            uint64_t t = os_gettime_ns();

            if (t >= d_ptr->dbr_inc_timeout) {
                d_ptr->dbr_inc_timeout = 0;
                dbr_inc_bitrate();
                dbr_set_bitrate();
            }
        }
    }

    if (num_packets < 5) {
        if (!pframes)
            d_ptr->congestion = 0.0f;
        return;
    }

    auto first = find_first_video_packet();
    if (!first)
        return;

    /* if the amount of time stored in the buffered packets waiting to be
     * sent is higher than threshold, drop frames */
    auto buffer_duration_usec = d_ptr->last_dts_usec - first->dts_usec;

    if (!pframes) {
        d_ptr->congestion =
            (float)buffer_duration_usec / (float)drop_threshold;
    }

    /* alternatively, drop only pframes:
     * (!pframes && stream->dbr_enabled)
     * but let's test without dropping frames
     * at all first */
    if (d_ptr->dbr_enabled) {
        bool bitrate_changed = false;

        if (pframes) {
            return;
        }

        if ((uint64_t)buffer_duration_usec >= DBR_TRIGGER_USEC) {
            std::lock_guard<std::mutex> lock(d_ptr->dbr_mutex);
            bitrate_changed = dbr_bitrate_lowered();
        }

        if (bitrate_changed) {
            blog(LOG_DEBUG, "buffer_duration_msec: %lld", buffer_duration_usec / 1000);
            dbr_set_bitrate();
        }
        return;
    }

    if (buffer_duration_usec > drop_threshold) {
        blog(LOG_DEBUG, "buffer_duration_usec: %lld", buffer_duration_usec);
        drop_frames(name, priority, pframes);
    }
}

bool rtmp_stream_output::add_video_packet(const std::shared_ptr<encoder_packet> &packet)
{
    check_to_drop_frames(false);
    check_to_drop_frames(true);

    /* if currently dropping frames, drop packets until it reaches the
     * desired priority */
    if (packet->drop_priority < d_ptr->min_priority) {
        d_ptr->dropped_frames++;
        return false;
    } else {
        d_ptr->min_priority = 0;
    }

    d_ptr->last_dts_usec = packet->dts_usec;
    return add_packet(packet);
}

void rtmp_stream_output::i_encoded_packet(std::shared_ptr<encoder_packet> packet)
{
    if (d_ptr->disconnected || !d_ptr->active)
        return;

    /* encoder fail */
    if (!packet) {
        d_ptr->encode_error = true;
        os_sem_post(d_ptr->send_sem);
        return;
    }

    std::shared_ptr<encoder_packet> new_packet;
    if (packet->type == obs_encoder_type::OBS_ENCODER_VIDEO) {
        if (!d_ptr->got_first_video) {
            d_ptr->start_dts_offset = get_ms_time(packet, packet->dts);
            d_ptr->got_first_video = true;
        }

        new_packet = obs_parse_avc_packet(packet);
    } else {
        new_packet = packet;
    }

    d_ptr->packets_mutex.lock();

    bool added_packet = false;
    if (!d_ptr->disconnected) {
        added_packet = (packet->type == obs_encoder_type::OBS_ENCODER_VIDEO)
                           ? add_video_packet(new_packet)
                           : add_packet(new_packet);
    }

    d_ptr->packets_mutex.unlock();

    if (added_packet)
        os_sem_post(d_ptr->send_sem);
}

uint64_t rtmp_stream_output::i_get_total_bytes()
{
    return d_ptr->total_bytes_sent;
}

int rtmp_stream_output::i_get_dropped_frames()
{
    return d_ptr->dropped_frames;
}


void rtmp_stream_output::free_packets()
{
    std::lock_guard<std::mutex> lock(d_ptr->packets_mutex);

    auto num_packets = d_ptr->packets.size();
    if (num_packets)
        blog(LOG_INFO, "Freeing %d remaining packets", (int)num_packets);

    d_ptr->packets.clear();
}
