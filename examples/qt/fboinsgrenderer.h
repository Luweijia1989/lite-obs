// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef FBOINSGRENDERER_H
#define FBOINSGRENDERER_H

#include <QtQuick/QQuickFramebufferObject>
#include <QOpenGLFramebufferObject>
#include "logorenderer.h"

class LogoInFboRenderer : public QObject, public QQuickFramebufferObject::Renderer
{
    Q_OBJECT
public:
    LogoInFboRenderer()
    {
        logo.initialize();
    }

    ~LogoInFboRenderer() {
    }

    void render() override {
        logo.render();
        update();
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        auto fbo = new QOpenGLFramebufferObject(size, format);
        emit newTexture(fbo->texture(), size.width(), size.height());
        return fbo;
    }

    LogoRenderer logo;

signals:
    void newTexture(int texId, uint32_t texWidth, uint32_t texHeight);
};


class FboInSGRenderer : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Renderer)
public:
    FboInSGRenderer();
    Renderer *createRenderer() const;

signals:
    void newTexture(int texId, uint32_t texWidth, uint32_t texHeight);
};

#endif
