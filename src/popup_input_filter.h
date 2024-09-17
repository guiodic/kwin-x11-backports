/*
    SPDX-FileCopyrightText: 2017 Martin Graesslin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

*/
#pragma once

#include "input.h"

#include <QList>
#include <QObject>

namespace KWin
{
class Window;

class PopupInputFilter : public QObject, public InputEventFilter
{
    Q_OBJECT
public:
    explicit PopupInputFilter();
    bool pointerEvent(MouseEvent *event, quint32 nativeButton) override;
    bool keyEvent(KeyEvent *event) override;
    bool touchDown(qint32 id, const QPointF &pos, std::chrono::microseconds time) override;
    bool tabletToolEvent(TabletEvent *event) override;

private:
    void handleWindowAdded(Window *client);

    void focus(Window *popup);
    void cancelPopups();

    QList<Window *> m_popupWindows;
};
}
