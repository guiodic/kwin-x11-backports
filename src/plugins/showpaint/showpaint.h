/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "libkwineffects/kwineffects.h"

namespace KWin
{

class GLVertexBuffer;

class ShowPaintEffect : public Effect
{
    Q_OBJECT

public:
    ShowPaintEffect();
    ~ShowPaintEffect() override;

    void paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, EffectScreen *screen) override;
    void paintWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, QRegion region, WindowPaintData &data) override;

    bool isActive() const override;

private Q_SLOTS:
    void toggle();

private:
    void paintGL(const QMatrix4x4 &projection, qreal scale);
    void paintQPainter();

    bool m_active = false;
    QRegion m_painted; // what's painted in one pass
    int m_colorIndex = 0;
    std::unique_ptr<GLVertexBuffer> m_vbo;
};

} // namespace KWin
