#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickView>
#include <QDebug>
#include <QSurfaceFormat>
#include "lite-obs-example.h"
#include "fboinsgrenderer.h"

static void log_handler(int log_level, const char *msg)
{
    qDebug() << msg;
}

int main(int argc, char *argv[])
{
    qRegisterMetaType<uint32_t>("uint32_t");

    lite_obs_set_log_handle(log_handler);

    qmlRegisterType<FboInSGRenderer>("com.ypp", 1, 0, "Render");

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QGuiApplication app(argc, argv);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QSurfaceFormat format;
    format.setMajorVersion(3);
    format.setMinorVersion(QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGL ? 3 : 0);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    LiteObsExample example;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("example", &example);
    QQuickView view(&engine, nullptr);
    QObject::connect(&view, &QQuickView::sceneGraphInitialized, [&example](){
        //reset here for share texture
        example.resetLiteObs(1280, 720, 20);
    });

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    view.setSource(url);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(400, 1280);
    view.show();

    return app.exec();
}
