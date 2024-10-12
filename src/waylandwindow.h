/*
    SPDX-FileCopyrightText: 2020 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "window.h"

namespace KWin
{

class WaylandWindow : public Window
{
    Q_OBJECT

public:
    WaylandWindow(SurfaceInterface *surface);

    QString captionNormal() const override;
    QString captionSuffix() const override;
    pid_t pid() const override;
    bool isClient() const override;
    bool isLockScreen() const override;
    bool isLocalhost() const override;
    BoxF resizeWithChecks(const BoxF &geometry, const QSizeF &size) override;
    void killWindow() override;
    QString windowRole() const override;

    virtual BoxF frameRectToBufferRect(const BoxF &rect) const;

    void setCaption(const QString &caption);

protected:
    bool belongsToSameApplication(const Window *other, SameApplicationChecks checks) const override;
    bool belongsToDesktop() const override;
    void doSetActive() override;
    void updateCaption() override;
    void updateClientOutputs();
    std::unique_ptr<WindowItem> createItem(Item *parentItem) override;

    void cleanGrouping();
    void updateGeometry(const BoxF &rect);
    void markAsMapped();

private:
    void updateIcon();
    void updateResourceName();

    QString m_captionNormal;
    QString m_captionSuffix;
    bool m_isScreenLocker = false;
};

} // namespace KWin
