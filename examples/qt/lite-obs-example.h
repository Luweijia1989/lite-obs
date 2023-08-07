#pragma once

#include <QObject>
#include <thread>
#include <lite-obs/lite_obs.h>

class LiteObsExample : public QObject
{
    Q_OBJECT
public:
    LiteObsExample(QObject *parent = nullptr);
    ~LiteObsExample();
    void resetLiteObs(int width, int height, int fps);

public slots:
    void doAudioMixTest(bool start);
    void doStartOutput();
    void doStopOutput();
    void doTextureMix(int id, uint32_t width, uint32_t height);

private:
    std::shared_ptr<lite_obs> m_liteObs{};
    std::thread m_audioTestThread;
    bool audioTestRunning{};
};
