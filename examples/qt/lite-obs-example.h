#pragma once

#include <QObject>
#include <thread>

class lite_obs;
class LiteObsExample : public QObject
{
    Q_OBJECT
public:
    LiteObsExample(QObject *parent = nullptr);
    void resetLiteObs(int width, int height, int fps);

public slots:
    void doAudioMixTest(bool start);
    void doStartOutput();
    void doStopOutput();

private:
    std::shared_ptr<lite_obs> m_liteObs{};
    std::thread m_audioTestThread;
};
