/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "x11_standalone_xinputintegration.h"
#include "core/outputbackend.h"
#include "ge_event_mem_mover.h"
#include "gestures.h"
#include "main.h"
#include "screenedge.h"
#include "x11_standalone_cursor.h"
#include "x11_standalone_logging.h"

#include "effect/globals.h"
#include "effect/xcb.h"
#include "workspace.h"
#include "x11eventfilter.h"

#include <X11/extensions/XI2proto.h>
#include <X11/extensions/XInput2.h>

#include <linux/input.h>

namespace KWin
{

static inline qreal fixed1616ToReal(FP1616 val)
{
    return (val)*1.0 / (1 << 16);
}

class XInputEventFilter : public X11EventFilter
{
public:
    XInputEventFilter(int xi_opcode)
        : X11EventFilter(XCB_GE_GENERIC, xi_opcode, QList<int>{XI_RawMotion, XI_RawButtonPress, XI_RawButtonRelease, XI_RawKeyPress, XI_RawKeyRelease, XI_TouchBegin, XI_TouchUpdate, XI_TouchOwnership, XI_TouchEnd})
    {
    }
    ~XInputEventFilter() override = default;

    bool event(xcb_generic_event_t *event) override
    {
        GeEventMemMover ge(event);
        switch (ge->event_type) {
        case XI_RawButtonPress:
        case XI_RawButtonRelease:
        case XI_RawMotion: {
            if (m_x11Cursor) {
                m_x11Cursor->notifyCursorPosChanged();
            }
        } break;
        case XI_TouchBegin: {
            auto e = reinterpret_cast<xXIDeviceEvent *>(event);
            m_lastTouchPositions.insert(e->detail, QPointF(fixed1616ToReal(e->event_x), fixed1616ToReal(e->event_y)));
            break;
        }
        case XI_TouchUpdate: {
            auto e = reinterpret_cast<xXIDeviceEvent *>(event);
            const QPointF touchPosition = QPointF(fixed1616ToReal(e->event_x), fixed1616ToReal(e->event_y));
            if (e->detail == m_trackingTouchId) {
                const auto last = m_lastTouchPositions.value(e->detail);
                workspace()->screenEdges()->gestureRecognizer()->updateSwipeGesture(touchPosition - last);
            }
            m_lastTouchPositions.insert(e->detail, touchPosition);
            break;
        }
        case XI_TouchEnd: {
            auto e = reinterpret_cast<xXIDeviceEvent *>(event);
            if (e->detail == m_trackingTouchId) {
                workspace()->screenEdges()->gestureRecognizer()->endSwipeGesture();
            }
            m_lastTouchPositions.remove(e->detail);
            m_trackingTouchId = 0;
            break;
        }
        case XI_TouchOwnership: {
            auto e = reinterpret_cast<xXITouchOwnershipEvent *>(event);
            auto it = m_lastTouchPositions.constFind(e->touchid);
            if (it == m_lastTouchPositions.constEnd()) {
                XIAllowTouchEvents(display(), e->deviceid, e->sourceid, e->touchid, XIRejectTouch);
            } else {
                if (workspace()->screenEdges()->gestureRecognizer()->startSwipeGesture(it.value()) > 0) {
                    m_trackingTouchId = e->touchid;
                }
                XIAllowTouchEvents(display(), e->deviceid, e->sourceid, e->touchid, m_trackingTouchId == e->touchid ? XIAcceptTouch : XIRejectTouch);
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

    void setCursor(const QPointer<X11Cursor> &cursor)
    {
        m_x11Cursor = cursor;
    }
    void setDisplay(::Display *display)
    {
        m_x11Display = display;
    }

private:
    ::Display *display() const
    {
        return m_x11Display;
    }

    QPointer<X11Cursor> m_x11Cursor;
    ::Display *m_x11Display = nullptr;
    uint32_t m_trackingTouchId = 0;
    QHash<uint32_t, QPointF> m_lastTouchPositions;
};

XInputIntegration::XInputIntegration(::Display *display, QObject *parent)
    : QObject(parent)
    , m_x11Display(display)
{
}

XInputIntegration::~XInputIntegration() = default;

void XInputIntegration::init()
{
    ::Display *dpy = display();
    int xi_opcode, event, error;
    // init XInput extension
    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) {
        qCDebug(KWIN_X11STANDALONE) << "XInputExtension not present";
        return;
    }

    // verify that the XInput extension is at at least version 2.0
    int major = 2, minor = 2;
    int result = XIQueryVersion(dpy, &major, &minor);
    if (result != Success) {
        qCDebug(KWIN_X11STANDALONE) << "Failed to init XInput 2.2, trying 2.0";
        minor = 0;
        if (XIQueryVersion(dpy, &major, &minor) != Success) {
            qCDebug(KWIN_X11STANDALONE) << "Failed to init XInput";
            return;
        }
    }
    m_hasXInput = true;
    m_xiOpcode = xi_opcode;
    m_majorVersion = major;
    m_minorVersion = minor;
    qCDebug(KWIN_X11STANDALONE) << "Has XInput support" << m_majorVersion << "." << m_minorVersion;
}

void XInputIntegration::setCursor(X11Cursor *cursor)
{
    m_x11Cursor = QPointer<X11Cursor>(cursor);
}

void XInputIntegration::startListening()
{
    // this assumes KWin is the only one setting events on the root window
    // given Qt's source code this seems to be true. If it breaks, we need to change
    XIEventMask evmasks[1];
    unsigned char mask1[XIMaskLen(XI_LASTEVENT)];

    memset(mask1, 0, sizeof(mask1));

    XISetMask(mask1, XI_RawButtonPress);
    XISetMask(mask1, XI_RawButtonRelease);
    XISetMask(mask1, XI_RawMotion);
    if (m_majorVersion >= 2 && m_minorVersion >= 2) {
        // touch events since 2.2
        XISetMask(mask1, XI_TouchBegin);
        XISetMask(mask1, XI_TouchUpdate);
        XISetMask(mask1, XI_TouchOwnership);
        XISetMask(mask1, XI_TouchEnd);
    }

    evmasks[0].deviceid = XIAllMasterDevices;
    evmasks[0].mask_len = sizeof(mask1);
    evmasks[0].mask = mask1;
    XISelectEvents(display(), rootWindow(), evmasks, 1);

    m_xiEventFilter = std::make_unique<XInputEventFilter>(m_xiOpcode);
    m_xiEventFilter->setCursor(m_x11Cursor);
    m_xiEventFilter->setDisplay(display());
}

}

#include "moc_x11_standalone_xinputintegration.cpp"
