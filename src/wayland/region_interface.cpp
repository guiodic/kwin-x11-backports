/********************************************************************
Copyright 2014  Martin Gräßlin <mgraesslin@kde.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) version 3, or any
later version accepted by the membership of KDE e.V. (or its
successor approved by the membership of KDE e.V.), which shall
act as a proxy defined in Section 6 of version 3 of the license.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "region_interface.h"
#include "compositor_interface.h"
// Wayland
#include <wayland-server.h>

namespace KWayland
{
namespace Server
{

class RegionInterface::Private
{
public:
    Private(CompositorInterface *compositor, RegionInterface *q);
    ~Private();
    void create(wl_client *client, quint32 version, quint32 id);
    CompositorInterface *compositor;
    wl_resource *region = nullptr;
    QRegion qtRegion;

    static RegionInterface *get(wl_resource *native);

private:
    void add(const QRect &rect);
    void subtract(const QRect &rect);

    static void unbind(wl_resource *r);
    static void destroyCallback(wl_client *client, wl_resource *r);
    static void addCallback(wl_client *client, wl_resource *r, int32_t x, int32_t y, int32_t width, int32_t height);
    static void subtractCallback(wl_client *client, wl_resource *r, int32_t x, int32_t y, int32_t width, int32_t height);

    static Private *cast(wl_resource *r) {
        return reinterpret_cast<Private*>(wl_resource_get_user_data(r));
    }

    static const struct wl_region_interface s_interface;
    RegionInterface *q;
};

const struct wl_region_interface RegionInterface::Private::s_interface = {
    destroyCallback,
    addCallback,
    subtractCallback
};

RegionInterface::Private::Private(CompositorInterface *compositor, RegionInterface *q)
    : compositor(compositor)
    , q(q)
{
}

RegionInterface::Private::~Private()
{
    if (region) {
        wl_resource_destroy(region);
    }
}

void RegionInterface::Private::add(const QRect &rect)
{
    qtRegion = qtRegion.united(rect);
    emit q->regionChanged(qtRegion);
}

void RegionInterface::Private::subtract(const QRect &rect)
{
    if (qtRegion.isEmpty()) {
        return;
    }
    qtRegion = qtRegion.subtracted(rect);
    emit q->regionChanged(qtRegion);
}

void RegionInterface::Private::addCallback(wl_client *client, wl_resource *r, int32_t x, int32_t y, int32_t width, int32_t height)
{
    Q_UNUSED(client)
    cast(r)->add(QRect(x, y, width, height));
}

void RegionInterface::Private::subtractCallback(wl_client *client, wl_resource *r, int32_t x, int32_t y, int32_t width, int32_t height)
{
    Q_UNUSED(client)
    cast(r)->subtract(QRect(x, y, width, height));
}

void RegionInterface::Private::destroyCallback(wl_client *client, wl_resource *r)
{
    Q_UNUSED(client)
    cast(r)->q->deleteLater();
}

void RegionInterface::Private::unbind(wl_resource *r)
{
    auto region = cast(r);
    region->region = nullptr;
    region->q->deleteLater();
}

void RegionInterface::Private::create(wl_client *client, quint32 version, quint32 id)
{
    Q_ASSERT(!region);
    region = wl_resource_create(client, &wl_region_interface, version, id);
    if (!region) {
        return;
    }
    wl_resource_set_implementation(region, &s_interface, this, unbind);
}

RegionInterface *RegionInterface::Private::get(wl_resource *native)
{
    if (!native) {
        return nullptr;
    }
    return cast(native)->q;
}

RegionInterface::RegionInterface(CompositorInterface *parent)
    : QObject(/*parent*/)
    , d(new Private(parent, this))
{
}

RegionInterface::~RegionInterface() = default;

void RegionInterface::create(wl_client *client, quint32 version, quint32 id)
{
    d->create(client, version, id);
}

QRegion RegionInterface::region() const
{
    return d->qtRegion;
}

wl_resource *RegionInterface::resource() const
{
    return d->region;
}

RegionInterface *RegionInterface::get(wl_resource *native)
{
    return Private::get(native);
}

}
}
