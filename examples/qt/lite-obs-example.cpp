#include "lite-obs-example.h"
#include <QDebug>
#include <QStandardPaths>
#include <QFile>
#include <QRandomGenerator>
#include <QThread>

class output_callback : public lite_obs_output_callbak
{
public:
    virtual void start(){qDebug() << "===start";}
    virtual void stop(int code, std::string msg){qDebug() << "===stop";}
    virtual void starting(){qDebug() << "===starting";}
    virtual void stopping(){qDebug() << "===stopping";}
    virtual void activate(){qDebug() << "===activate";}
    virtual void deactivate(){qDebug() << "===deactivate";}
    virtual void reconnect(){qDebug() << "===reconnect";}
    virtual void reconnect_success(){qDebug() << "===reconnect_success";}
    virtual void connected() {qDebug() << "===connected";}
    virtual void first_media_packet() {qDebug() << "===first_media_packet";}
};

LiteObsExample::LiteObsExample(QObject *parent)
    : QObject(parent)
    , m_liteObs(std::make_shared<lite_obs>())
{
    qDebug() << "main thread id: " << QThread::currentThreadId();
}

LiteObsExample::~LiteObsExample()
{
    audioTestRunning = false;
    if (m_audioTestThread.joinable())
        m_audioTestThread.join();
}

void LiteObsExample::resetLiteObs(int width, int height, int fps)
{
    m_liteObs->obs_reset_video(width, height, fps);
    m_liteObs->obs_reset_audio(48000);
}

void LiteObsExample::doAudioMixTest(bool start)
{
    if (start) {
        audioTestRunning = true;
        m_audioTestThread = std::thread([=](){
            auto source = m_liteObs->lite_obs_create_source(source_type::Source_Audio);

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
                source->output_audio(data, 441 * 2, audio_format::AUDIO_FORMAT_FLOAT, speaker_layout::SPEAKERS_STEREO, 44100);

                QThread::msleep(QRandomGenerator::global()->bounded(17, 22));
                if (!audioTestRunning) {
                    qDebug() << "audio mix test stop.";
                    break;
                }
            }

            m_liteObs->lite_obs_destroy_source(source);
        });
    } else {
        audioTestRunning = false;
        if (m_audioTestThread.joinable())
            m_audioTestThread.join();
    }
}

void LiteObsExample::doStartOutput()
{
    auto callback = std::make_shared<output_callback>();
//    auto path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
//    path = path + "/output.flv";
    QString path = "rtmp://192.168.16.28/live/test";
    m_liteObs->lite_obs_start_output(path.toStdString(), 4000, 160, callback);
}

void LiteObsExample::doStopOutput()
{
    m_liteObs->lite_obs_stop_output();
}

void LiteObsExample::doTextureMix(int id, uint32_t width, uint32_t height)
{
    qDebug() << "texture mix: " << id << width <<height << QThread::currentThreadId();
    static lite_obs_media_source *source{};
    if (!source)
        source = m_liteObs->lite_obs_create_source(source_type::Source_Video);

    source->output_video(id, width, height);
}
