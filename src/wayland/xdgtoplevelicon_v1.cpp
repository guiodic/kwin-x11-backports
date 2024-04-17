/*
    SPDX-FileCopyrightText: 2024 David Edmundson <davidedmundson@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "xdgtoplevelicon_v1.h"
#include "display.h"
#include "xdgshell.h"
#include "xdgshell_p.h"

#include "core/graphicsbufferview.h"

#include <qwayland-server-xdg-toplevel-icon-v1.h>

#include <QDebug>
#include <QPointer>

#include <memory>

namespace KWin
{
constexpr int version = 1;

class XdgToplevelIconV1InterfacePrivate : public QtWaylandServer::xdg_toplevel_icon_v1
{
public:
    XdgToplevelIconV1InterfacePrivate(XdgToplevelIconV1Interface *q, Display *display)
        : xdg_toplevel_icon_v1(*display, version)
        , m_display(display)
    {
    }

protected:
    // Dave this is missing in the spec
    // I thought there was an autogenerated thing now? Did I make that up?

    // void xdg_toplevel_icon_v1_destroy(Resource *resource) override
    // {
    //     wl_resource_destroy(resource->handle);
    // }

    void xdg_toplevel_icon_v1_get_icon_sizes(Resource *resource) override
    {
        // We have no meaningful sizes to send here.
        // We don't know what size plasma or any plugins will render at

        // pick something big in case someone uses it.
        // We know nothing does currently.

        send_icon_size(96, 1);

        // And if we are going to have this, why not just be on bind?
        send_done(resource->handle);
    }

    void xdg_toplevel_icon_v1_set_icon_name(Resource *resource, struct ::wl_resource *toplevel, const QString &icon_name) override
    {
        XdgToplevelInterfacePrivate *toplevelPrivate = XdgToplevelInterfacePrivate::get(toplevel);
        if (!toplevelPrivate) {
            wl_resource_post_error(resource->handle, WL_DISPLAY_ERROR_INVALID_OBJECT, "xdg_toplevel not found");
            return;
        }
        // setting the name always changes the icon
        if (icon_name.isEmpty()) {
            toplevelPrivate->customIcon = QIcon();
        } else {
            toplevelPrivate->customIcon = QIcon::fromTheme(icon_name);
        }
        Q_EMIT toplevelPrivate->q->customIconChanged();
    }

    void xdg_toplevel_icon_v1_set_icon_buffer(Resource *resource, struct ::wl_resource *toplevel, struct ::wl_resource *icon, int32_t scale) override
    {
        XdgToplevelInterfacePrivate *toplevelPrivate = XdgToplevelInterfacePrivate::get(toplevel);
        if (!toplevelPrivate) {
            wl_resource_post_error(resource->handle, WL_DISPLAY_ERROR_INVALID_OBJECT, "xdg_toplevel not found");
            return;
        }
        GraphicsBuffer *buffer = m_display->bufferForResource(icon);
        Q_ASSERT(buffer);

        GraphicsBufferView view(buffer);

        QImage image = view.image()->copy();
        if (image.isNull()) {
            wl_resource_post_error(resource->handle, XdgToplevelIconV1InterfacePrivate::error_wrong_format, "TopLevel icon invalid");
            return;
        }
        if (image.width() != image.height()) {
            wl_resource_post_error(resource->handle, XdgToplevelIconV1InterfacePrivate::error_wrong_format, "TopLevel icon is not a square");
            return;
        }

        image.setDevicePixelRatio(scale);

        // adding a buffer adds it to the existing icon
        if (!toplevelPrivate->customIcon.name().isEmpty()) {
            // we already found and reserved a named icon
            // we can ignore the buffer
            return;
        }
        toplevelPrivate->customIcon.addPixmap(QPixmap::fromImage(image));
        Q_EMIT toplevelPrivate->q->customIconChanged();
    }

private:
    Display *m_display;
};

XdgToplevelIconV1Interface::XdgToplevelIconV1Interface(Display *display, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<XdgToplevelIconV1InterfacePrivate>(this, display))
{
}

XdgToplevelIconV1Interface::~XdgToplevelIconV1Interface() = default;
}

// #include "moc_xdgtoplevelicon_v1.cpp"
