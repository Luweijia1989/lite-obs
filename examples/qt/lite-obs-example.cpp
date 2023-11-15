#include "lite-obs-example.h"
#include <QDebug>
#include <QStandardPaths>
#include <QFile>
#include <QRandomGenerator>
#include <QThread>

LiteObsExample::LiteObsExample(QObject *parent)
    : QObject(parent)
{
    m_liteObs = lite_obs_api_new();
    qDebug() << "main thread id: " << QThread::currentThreadId();
}

LiteObsExample::~LiteObsExample()
{
    audioTestRunning = false;
    if (m_audioTestThread.joinable())
        m_audioTestThread.join();

    videoTestRunning = false;
    if (m_videoTestThread.joinable())
        m_videoTestThread.join();

    lite_obs_api_delete(&m_liteObs);
}

void LiteObsExample::resetLiteObs(int width, int height, int fps)
{
    m_liteObs->lite_obs_reset_video(m_liteObs, width, height, fps);
    m_liteObs->lite_obs_reset_audio(m_liteObs, 48000);
}

void LiteObsExample::doAudioMixTest(bool start)
{
    if (start) {
        audioTestRunning = true;
        m_audioTestThread = std::thread([=](){
            auto source = lite_obs_media_source_new(m_liteObs, source_type::SOURCE_AUDIO);

            QFile audiofile(":/resource/44100_2_float.pcm");
            audiofile.open(QFile::ReadOnly);
            auto alldata = audiofile.readAll();
            int index = 0;
            auto tt = 441 * 2 * sizeof(float) * 2;
            while (true) {
                if (alldata.size() - tt * index < tt)
                    index = 0;

                const uint8_t *data[MAX_AV_PLANES] = {};
                data[0] = (uint8_t *)alldata.data() + tt * (index++);
                source->output_audio(source, data, 441 * 2, audio_format::AUDIO_FORMAT_FLOAT, speaker_layout::SPEAKERS_STEREO, 44100);

                QThread::msleep(QRandomGenerator::global()->bounded(17, 22));
                if (!audioTestRunning) {
                    qDebug() << "audio mix test stop.";
                    break;
                }
            }

            lite_obs_media_source_delete(m_liteObs, &source);
        });
    } else {
        audioTestRunning = false;
        if (m_audioTestThread.joinable())
            m_audioTestThread.join();
    }
}

void LiteObsExample::doVideoFrameMixTest(bool start)
{
    if (start) {
        videoTestRunning = true;
        m_videoTestThread = std::thread([=](){
            auto source = lite_obs_media_source_new(m_liteObs, source_type::SOURCE_ASYNCVIDEO);

            QFile audiofile(":/resource/640360420p.yuv");
            audiofile.open(QFile::ReadOnly);
            auto alldata = audiofile.readAll();
            int index = 0;
            int tt = 640 * 360 * 1.5;
            while (true) {
                if (alldata.size() - tt * index < tt)
                    index = 0;

                uint8_t *p = (uint8_t *)alldata.data() + tt * (index++);
                const uint8_t *data[MAX_AV_PLANES] = {};
                data[0] = p;
                data[1] = p + 640 * 360;
                data[2] = p + 640 * 360 * 5 / 4;

                int linesize[MAX_AV_PLANES] = {};
                linesize[0] = 640;
                linesize[1] = 320;
                linesize[2] = 320;

                source->output_video2(source, data, linesize, VIDEO_FORMAT_I420, VIDEO_RANGE_FULL, VIDEO_CS_709, 640, 360);

                QThread::msleep(50);
                if (!videoTestRunning) {
                    qDebug() << "video frame mix test stop.";
                    break;
                }
            }

            source->clear_video(source);
            lite_obs_media_source_delete(m_liteObs, &source);
        });
    } else {
        videoTestRunning = false;
        if (m_videoTestThread.joinable())
            m_videoTestThread.join();
    }
}

void LiteObsExample::doStartOutput()
{
    lite_obs_output_callbak cb{};
    cb.start = [](void *){qDebug() << "===start";};
    cb.stop = [](int code, const char *msg, void *){qDebug() << "===stop";};
    cb.starting = [](void *){qDebug() << "===starting";};
    cb.stopping = [](void *){qDebug() << "===stopping";};
    cb.activate = [](void *){qDebug() << "===activate";};
    cb.deactivate = [](void *){qDebug() << "===deactivate";};
    cb.reconnect = [](void *){qDebug() << "===reconnect";};
    cb.reconnect_success = [](void *){qDebug() << "===reconnect_success";};
    cb.connected = [](void *){qDebug() << "===connected";};
    cb.first_media_packet = [](void *){qDebug() << "===first_media_packet";};
    cb.opaque = this;

//    auto path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
//    path = path + "/output.flv";
    QString path = "rtmp://192.168.16.28/live/test";
    m_liteObs->lite_obs_start_output(m_liteObs, path.toStdString().c_str(), 4000, 160, cb);
}

void LiteObsExample::doStopOutput()
{
    m_liteObs->lite_obs_stop_output(m_liteObs);
}

static lite_obs_media_source_api *source{};
void LiteObsExample::doTextureMix(int id, uint32_t width, uint32_t height)
{
    qDebug() << "texture mix: " << id << width <<height << QThread::currentThreadId();

    if (!source)
        source = lite_obs_media_source_new(m_liteObs, source_type::SOURCE_VIDEO);

    source->output_video(source, id, width, height);
//    source->set_scale(0.2f, 0.2f);
//    source->set_pos(100, 100);
    source->set_render_box(source, 50, 100, 600, 600, source_aspect_ratio_mode::IGNORE_ASPECT_RATIO);
}

void LiteObsExample::setSourceOrder(int order)
{
    if (source)
        source->set_order(source, (order_movement)order);
}

void LiteObsExample::resetEncoderType(bool sw)
{
    m_liteObs->lite_obs_reset_encoder(m_liteObs, sw);
}
