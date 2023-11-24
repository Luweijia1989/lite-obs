// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "fboinsgrenderer.h"

FboInSGRenderer::FboInSGRenderer()
{
}

QQuickFramebufferObject::Renderer *FboInSGRenderer::createRenderer() const
{
    auto render =  new LogoInFboRenderer();
    connect(render, &LogoInFboRenderer::newTexture, this, &FboInSGRenderer::newTexture, Qt::DirectConnection);
    return render;
}
