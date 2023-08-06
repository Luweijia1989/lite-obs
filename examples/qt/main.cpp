#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickView>
#include <QDebug>
#include <QSurfaceFormat>
#include "lite-obs-example.h"
#include "fboinsgrenderer.h"

int main(int argc, char *argv[])
{
    qmlRegisterType<FboInSGRenderer>("com.ypp", 1, 0, "Render");

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);

    LiteObsExample example;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("example", &example);
    QQuickView view(&engine, nullptr);
#ifdef Q_OS_MACOS
    QSurfaceFormat format;
    format.setMajorVersion(3);
    format.setMinorVersion(3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    view.setFormat(format);
#endif
    QObject::connect(&view, &QQuickView::sceneGraphInitialized, [&example](){
        //reset here for share texture
        example.resetLiteObs(1280, 720, 20);
    });

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    view.setSource(url);
    view.show();

    return app.exec();
}
