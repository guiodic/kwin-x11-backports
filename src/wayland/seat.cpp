/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2020 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "seat.h"
#include "abstract_data_source.h"
#include "datacontroldevice_v1.h"
#include "datacontrolsource_v1.h"
#include "datadevice.h"
#include "datadevice_p.h"
#include "datasource.h"
#include "display.h"
#include "display_p.h"
#include "keyboard.h"
#include "keyboard_p.h"
#include "pointer.h"
#include "pointer_p.h"
#include "pointerconstraints_v1.h"
#include "pointergestures_v1_p.h"
#include "primaryselectiondevice_v1.h"
#include "primaryselectionsource_v1.h"
#include "relativepointer_v1_p.h"
#include "seat_p.h"
#include "surface.h"
#include "textinput_v1_p.h"
#include "textinput_v2_p.h"
#include "textinput_v3_p.h"
#include "touch_p.h"
#include "utils/common.h"
#include "utils/resource.h"
#include "xdgtopleveldrag_v1.h"

#include <linux/input.h>

#include <functional>

namespace KWin
{
static const int s_version = 9;

/// Maps surface to the surface at @p pos, be it @p surface or one of its subsurfaces
static SurfaceInterface *mapToSurfaceInPosition(SurfaceInterface *surface, QPointF &pos)
{
    auto ret = surface->inputSurfaceAt(pos);
    if (ret && ret != surface) {
        pos = surface->mapToChild(ret, pos);
    }
    if (!ret) {
        ret = surface;
    }
    return ret;
}

SeatInterfacePrivate *SeatInterfacePrivate::get(SeatInterface *seat)
{
    return seat->d.get();
}

SeatInterfacePrivate::SeatInterfacePrivate(SeatInterface *q, Display *display)
    : QtWaylandServer::wl_seat(*display, s_version)
    , q(q)
    , display(display)
{
    textInputV1 = new TextInputV1Interface(q);
    textInputV2 = new TextInputV2Interface(q);
    textInputV3 = new TextInputV3Interface(q);
    pointer.reset(new PointerInterface(q));
    keyboard.reset(new KeyboardInterface(q));
    touch.reset(new TouchInterface(q));
}

void SeatInterfacePrivate::seat_bind_resource(Resource *resource)
{
    send_capabilities(resource->handle, capabilities);

    if (resource->version() >= WL_SEAT_NAME_SINCE_VERSION) {
        send_name(resource->handle, name);
    }
}

void SeatInterfacePrivate::seat_get_pointer(Resource *resource, uint32_t id)
{
    PointerInterfacePrivate *pointerPrivate = PointerInterfacePrivate::get(pointer.get());
    pointerPrivate->add(resource->client(), id, resource->version());
}

void SeatInterfacePrivate::seat_get_keyboard(Resource *resource, uint32_t id)
{
    KeyboardInterfacePrivate *keyboardPrivate = KeyboardInterfacePrivate::get(keyboard.get());
    keyboardPrivate->add(resource->client(), id, resource->version());
}

void SeatInterfacePrivate::seat_get_touch(Resource *resource, uint32_t id)
{
    TouchInterfacePrivate *touchPrivate = TouchInterfacePrivate::get(touch.get());
    touchPrivate->add(resource->client(), id, resource->version());
}

void SeatInterfacePrivate::seat_release(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

SeatInterface::SeatInterface(Display *display, QObject *parent)
    : QObject(parent)
    , d(new SeatInterfacePrivate(this, display))
{
    DisplayPrivate *displayPrivate = DisplayPrivate::get(d->display);
    displayPrivate->seats.append(this);
}

SeatInterface::~SeatInterface()
{
    if (d->display) {
        DisplayPrivate *displayPrivate = DisplayPrivate::get(d->display);
        displayPrivate->seats.removeOne(this);
    }
}

QList<DataDeviceInterface *> SeatInterfacePrivate::dataDevicesForSurface(SurfaceInterface *surface) const
{
    if (!surface) {
        return {};
    }
    QList<DataDeviceInterface *> primarySelectionDevices;
    for (auto it = dataDevices.constBegin(); it != dataDevices.constEnd(); ++it) {
        if ((*it)->client() == *surface->client()) {
            primarySelectionDevices << *it;
        }
    }
    return primarySelectionDevices;
}

void SeatInterfacePrivate::registerDataDevice(DataDeviceInterface *dataDevice)
{
    Q_ASSERT(dataDevice->seat() == q);
    dataDevices << dataDevice;
    auto dataDeviceCleanup = [this, dataDevice] {
        dataDevices.removeOne(dataDevice);
        globalKeyboard.focus.selections.removeOne(dataDevice);
    };
    QObject::connect(dataDevice, &QObject::destroyed, q, dataDeviceCleanup);
    QObject::connect(dataDevice, &DataDeviceInterface::selectionChanged, q, [this](DataSourceInterface *source, quint32 serial) {
        updateSelection(source, serial);
    });
    QObject::connect(dataDevice,
                     &DataDeviceInterface::dragStarted,
                     q,
                     [this](AbstractDataSource *source, SurfaceInterface *origin, quint32 serial, DragAndDropIcon *dragIcon) {
                         q->startDrag(source, origin, serial, dragIcon);
                     });
    // is the new DataDevice for the current keyoard focus?
    if (globalKeyboard.focus.surface) {
        // same client?
        if (*globalKeyboard.focus.surface->client() == dataDevice->client()) {
            globalKeyboard.focus.selections.append(dataDevice);
            if (currentSelection) {
                dataDevice->sendSelection(currentSelection);
            }
        }
    }
}

AbstractDropHandler *SeatInterface::dropHandlerForSurface(SurfaceInterface *surface) const
{
    auto list = d->dataDevicesForSurface(surface);
    if (list.isEmpty()) {
        return nullptr;
    };
    return list.first();
}

void SeatInterface::cancelDrag()
{
    if (d->drag.mode != SeatInterfacePrivate::Drag::Mode::None) {
        // cancel the drag, don't drop. serial does not matter
        d->cancelDrag();
    }
}

void SeatInterfacePrivate::registerDataControlDevice(DataControlDeviceV1Interface *dataDevice)
{
    Q_ASSERT(dataDevice->seat() == q);
    dataControlDevices << dataDevice;
    auto dataDeviceCleanup = [this, dataDevice] {
        dataControlDevices.removeOne(dataDevice);
    };
    QObject::connect(dataDevice, &QObject::destroyed, q, dataDeviceCleanup);

    QObject::connect(dataDevice, &DataControlDeviceV1Interface::selectionChanged, q, [this, dataDevice] {
        // Special klipper workaround to avoid a race
        // If the mimetype x-kde-onlyReplaceEmpty is set, and we've had another update in the meantime, do nothing
        // but resend selection to mimic normal event flow upon cancel and not confuse the client
        // See https://github.com/swaywm/wlr-protocols/issues/92
        const bool isKlipperEmptyReplacement = dataDevice->selection() && dataDevice->selection()->mimeTypes().contains(QLatin1String("application/x-kde-onlyReplaceEmpty"));
        if (isKlipperEmptyReplacement && currentSelection && !currentSelection->mimeTypes().isEmpty()) {
            dataDevice->selection()->cancel();
            dataDevice->sendSelection(currentSelection);
            return;
        }
        q->setSelection(dataDevice->selection(), display->nextSerial());
    });

    QObject::connect(dataDevice, &DataControlDeviceV1Interface::primarySelectionChanged, q, [this, dataDevice] {
        // Special klipper workaround to avoid a race
        // If the mimetype x-kde-onlyReplaceEmpty is set, and we've had another update in the meantime, do nothing
        // but resend selection to mimic normal event flow upon cancel and not confuse the client
        // See https://github.com/swaywm/wlr-protocols/issues/92
        const bool isKlipperEmptyReplacement = dataDevice->primarySelection() && dataDevice->primarySelection()->mimeTypes().contains(QLatin1String("application/x-kde-onlyReplaceEmpty"));
        if (isKlipperEmptyReplacement && currentPrimarySelection && !currentPrimarySelection->mimeTypes().isEmpty()) {
            dataDevice->primarySelection()->cancel();
            dataDevice->sendPrimarySelection(currentPrimarySelection);
            return;
        }
        q->setPrimarySelection(dataDevice->primarySelection(), display->nextSerial());
    });

    dataDevice->sendSelection(currentSelection);
    dataDevice->sendPrimarySelection(currentPrimarySelection);
}

void SeatInterfacePrivate::registerPrimarySelectionDevice(PrimarySelectionDeviceV1Interface *primarySelectionDevice)
{
    Q_ASSERT(primarySelectionDevice->seat() == q);

    primarySelectionDevices << primarySelectionDevice;
    auto dataDeviceCleanup = [this, primarySelectionDevice] {
        primarySelectionDevices.removeOne(primarySelectionDevice);
        globalKeyboard.focus.primarySelections.removeOne(primarySelectionDevice);
    };
    QObject::connect(primarySelectionDevice, &QObject::destroyed, q, dataDeviceCleanup);
    QObject::connect(primarySelectionDevice, &PrimarySelectionDeviceV1Interface::selectionChanged, q, [this](PrimarySelectionSourceV1Interface *source, quint32 serial) {
        updatePrimarySelection(source, serial);
    });
    // is the new DataDevice for the current keyoard focus?
    if (globalKeyboard.focus.surface) {
        // same client?
        if (*globalKeyboard.focus.surface->client() == primarySelectionDevice->client()) {
            globalKeyboard.focus.primarySelections.append(primarySelectionDevice);
            if (currentPrimarySelection) {
                primarySelectionDevice->sendSelection(currentPrimarySelection);
            }
        }
    }
}

void SeatInterfacePrivate::cancelDrag()
{
    QObject::disconnect(drag.dragSourceDestroyConnection);
    if (drag.source) {
        drag.source->dndCancelled();
    }
    if (drag.target) {
        drag.target->updateDragTarget(nullptr, 0);
        drag.target = nullptr;
    }
    drag = Drag();
    Q_EMIT q->dragSurfaceChanged();
    Q_EMIT q->dragEnded();
}

bool SeatInterfacePrivate::dragInhibitsPointer(SurfaceInterface *surface) const
{
    if (drag.mode != SeatInterfacePrivate::Drag::Mode::Pointer) {
        return false;
    }
    const bool targetHasDataDevice = !dataDevicesForSurface(surface).isEmpty();
    return targetHasDataDevice;
}

void SeatInterfacePrivate::endDrag()
{
    QObject::disconnect(drag.dragSourceDestroyConnection);

    AbstractDropHandler *dragTargetDevice = drag.target.data();
    AbstractDataSource *dragSource = drag.source;

    if (dragSource) {
        // TODO: Also check the current drag-and-drop action.
        if (dragTargetDevice && dragSource->isAccepted()) {
            Q_EMIT q->dragDropped();
            dragTargetDevice->drop();
            dragSource->dropPerformed();
        } else {
            dragSource->dropPerformed();
            dragSource->dndCancelled();
        }
    }

    if (dragTargetDevice) {
        dragTargetDevice->updateDragTarget(nullptr, 0);
    }

    drag = Drag();
    Q_EMIT q->dragSurfaceChanged();
    Q_EMIT q->dragEnded();
}

void SeatInterfacePrivate::updateSelection(DataSourceInterface *dataSource, quint32 serial)
{
    if (currentSelectionSerial - serial < UINT32_MAX / 2 && currentSelectionSerial != serial) {
        if (dataSource) {
            dataSource->cancel();
        }
        return;
    }
    q->setSelection(dataSource, serial);
}

void SeatInterfacePrivate::updatePrimarySelection(PrimarySelectionSourceV1Interface *dataSource, quint32 serial)
{
    if (currentPrimarySelectionSerial - serial < UINT32_MAX / 2 && currentPrimarySelectionSerial != serial) {
        if (dataSource) {
            dataSource->cancel();
        }
        return;
    }
    q->setPrimarySelection(dataSource, serial);
}

void SeatInterfacePrivate::sendCapabilities()
{
    const auto seatResources = resourceMap();
    for (SeatInterfacePrivate::Resource *resource : seatResources) {
        send_capabilities(resource->handle, capabilities);
    }
}

void SeatInterface::setHasKeyboard(bool has)
{
    if (hasKeyboard() == has) {
        return;
    }
    if (has) {
        d->capabilities |= SeatInterfacePrivate::capability_keyboard;
    } else {
        d->capabilities &= ~SeatInterfacePrivate::capability_keyboard;
    }

    d->sendCapabilities();
    Q_EMIT hasKeyboardChanged(has);
}

void SeatInterface::setHasPointer(bool has)
{
    if (hasPointer() == has) {
        return;
    }
    if (has) {
        d->capabilities |= SeatInterfacePrivate::capability_pointer;
    } else {
        d->capabilities &= ~SeatInterfacePrivate::capability_pointer;
    }

    d->sendCapabilities();
    Q_EMIT hasPointerChanged(has);
}

void SeatInterface::setHasTouch(bool has)
{
    if (hasTouch() == has) {
        return;
    }
    if (has) {
        d->capabilities |= SeatInterfacePrivate::capability_touch;
    } else {
        d->capabilities &= ~SeatInterfacePrivate::capability_touch;
    }

    d->sendCapabilities();
    Q_EMIT hasTouchChanged(has);
}

void SeatInterface::setName(const QString &name)
{
    if (d->name == name) {
        return;
    }
    d->name = name;

    const auto seatResources = d->resourceMap();
    for (SeatInterfacePrivate::Resource *resource : seatResources) {
        if (resource->version() >= WL_SEAT_NAME_SINCE_VERSION) {
            d->send_name(resource->handle, d->name);
        }
    }

    Q_EMIT nameChanged(d->name);
}

QString SeatInterface::name() const
{
    return d->name;
}

bool SeatInterface::hasPointer() const
{
    return d->capabilities & SeatInterfacePrivate::capability_pointer;
}

bool SeatInterface::hasKeyboard() const
{
    return d->capabilities & SeatInterfacePrivate::capability_keyboard;
}

bool SeatInterface::hasTouch() const
{
    return d->capabilities & SeatInterfacePrivate::capability_touch;
}

Display *SeatInterface::display() const
{
    return d->display;
}

SeatInterface *SeatInterface::get(wl_resource *native)
{
    if (SeatInterfacePrivate *seatPrivate = resource_cast<SeatInterfacePrivate *>(native)) {
        return seatPrivate->q;
    }
    return nullptr;
}

QPointF SeatInterface::pointerPos() const
{
    return d->globalPointer.pos;
}

void SeatInterface::notifyPointerMotion(const QPointF &pos)
{
    if (!d->pointer) {
        return;
    }
    if (d->globalPointer.pos == pos) {
        return;
    }
    d->globalPointer.pos = pos;
    Q_EMIT pointerPosChanged(pos);

    SurfaceInterface *focusedSurface = focusedPointerSurface();
    if (!focusedSurface) {
        return;
    }
    if (d->dragInhibitsPointer(focusedSurface)) {
        return;
    }
    if (focusedSurface->lockedPointer() && focusedSurface->lockedPointer()->isLocked()) {
        return;
    }

    QPointF localPosition = focusedPointerSurfaceTransformation().map(pos);
    SurfaceInterface *effectiveFocusedSurface = mapToSurfaceInPosition(focusedSurface, localPosition);

    if (d->pointer->focusedSurface() != effectiveFocusedSurface) {
        d->pointer->sendEnter(effectiveFocusedSurface, localPosition, display()->nextSerial());
        if (d->keyboard) {
            d->keyboard->setModifierFocusSurface(effectiveFocusedSurface);
        }
    }

    d->pointer->sendMotion(localPosition);
}

std::chrono::milliseconds SeatInterface::timestamp() const
{
    return d->timestamp;
}

void SeatInterface::setTimestamp(std::chrono::microseconds time)
{
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time);
    if (d->timestamp == milliseconds) {
        return;
    }
    d->timestamp = milliseconds;
    Q_EMIT timestampChanged();
}

void SeatInterface::setDragTarget(AbstractDropHandler *dropTarget,
                                  SurfaceInterface *surface,
                                  const QPointF &globalPosition,
                                  const QMatrix4x4 &inputTransformation)
{
    if (surface == d->drag.surface) {
        // no change
        return;
    }
    const quint32 serial = d->display->nextSerial();
    if (d->drag.target) {
        d->drag.target->updateDragTarget(nullptr, serial);
    }

    // TODO: technically we can have mulitple data devices
    // and we should send the drag to all of them, but that seems overly complicated
    // in practice so far the only case for mulitple data devices is for clipboard overriding
    d->drag.target = dropTarget;

    if (d->drag.mode == SeatInterfacePrivate::Drag::Mode::Pointer) {
        notifyPointerMotion(globalPosition);
        notifyPointerFrame();
    } else if (d->drag.mode == SeatInterfacePrivate::Drag::Mode::Touch && firstTouchPointPosition(surface) != globalPosition) {
        notifyTouchMotion(d->globalTouch.ids.begin()->second->serial, globalPosition);
    }

    if (d->drag.target) {
        QMatrix4x4 surfaceInputTransformation = inputTransformation;
        surfaceInputTransformation.scale(surface->scaleOverride());
        d->drag.surface = surface;
        d->drag.transformation = surfaceInputTransformation;
        if (d->dragInhibitsPointer(surface)) {
            notifyPointerLeave();
        }
        d->drag.target->updateDragTarget(surface, serial);
    } else {
        d->drag.surface = nullptr;
    }
    Q_EMIT dragSurfaceChanged();
    return;
}

void SeatInterface::setDragTarget(AbstractDropHandler *target, SurfaceInterface *surface, const QMatrix4x4 &inputTransformation)
{
    if (d->drag.mode == SeatInterfacePrivate::Drag::Mode::Pointer) {
        setDragTarget(target, surface, pointerPos(), inputTransformation);
    } else {
        Q_ASSERT(d->drag.mode == SeatInterfacePrivate::Drag::Mode::Touch);
        setDragTarget(target, surface, firstTouchPointPosition(surface), inputTransformation);
    }
}

SurfaceInterface *SeatInterface::focusedPointerSurface() const
{
    return d->globalPointer.focus.surface;
}

void SeatInterface::notifyPointerEnter(SurfaceInterface *surface, const QPointF &position, const QPointF &surfacePosition)
{
    QMatrix4x4 m;
    m.translate(-surfacePosition.x(), -surfacePosition.y());
    notifyPointerEnter(surface, position, m);
    if (d->globalPointer.focus.surface) {
        d->globalPointer.focus.offset = surfacePosition;
    }
}

void SeatInterface::notifyPointerEnter(SurfaceInterface *surface, const QPointF &position, const QMatrix4x4 &transformation)
{
    if (!d->pointer) {
        return;
    }
    if (d->dragInhibitsPointer(surface)) {
        // ignore
        return;
    }

    const quint32 serial = d->display->nextSerial();

    if (d->globalPointer.focus.surface) {
        disconnect(d->globalPointer.focus.destroyConnection);
    }
    d->globalPointer.focus = SeatInterfacePrivate::Pointer::Focus();
    d->globalPointer.focus.surface = surface;
    d->globalPointer.focus.destroyConnection = connect(surface, &QObject::destroyed, this, [this] {
        d->globalPointer.focus = SeatInterfacePrivate::Pointer::Focus();
    });
    d->globalPointer.focus.serial = serial;
    d->globalPointer.focus.transformation = transformation;
    d->globalPointer.focus.offset = QPointF();

    d->globalPointer.pos = position;
    QPointF localPosition = focusedPointerSurfaceTransformation().map(position);
    SurfaceInterface *effectiveFocusedSurface = mapToSurfaceInPosition(surface, localPosition);
    d->pointer->sendEnter(effectiveFocusedSurface, localPosition, serial);
    if (d->keyboard) {
        d->keyboard->setModifierFocusSurface(effectiveFocusedSurface);
    }
}

void SeatInterface::notifyPointerLeave()
{
    if (!d->pointer) {
        return;
    }

    if (d->globalPointer.focus.surface) {
        disconnect(d->globalPointer.focus.destroyConnection);
    }
    d->globalPointer.focus = SeatInterfacePrivate::Pointer::Focus();

    const quint32 serial = d->display->nextSerial();
    d->pointer->sendLeave(serial);
    if (d->keyboard) {
        d->keyboard->setModifierFocusSurface(nullptr);
    }
}

void SeatInterface::setFocusedPointerSurfacePosition(const QPointF &surfacePosition)
{
    if (d->globalPointer.focus.surface) {
        d->globalPointer.focus.offset = surfacePosition;
        d->globalPointer.focus.transformation = QMatrix4x4();
        d->globalPointer.focus.transformation.translate(-surfacePosition.x(), -surfacePosition.y());
    }
}

QPointF SeatInterface::focusedPointerSurfacePosition() const
{
    return d->globalPointer.focus.offset;
}

void SeatInterface::setFocusedPointerSurfaceTransformation(const QMatrix4x4 &transformation)
{
    if (d->globalPointer.focus.surface) {
        d->globalPointer.focus.transformation = transformation;
    }
}

QMatrix4x4 SeatInterface::focusedPointerSurfaceTransformation() const
{
    return d->globalPointer.focus.transformation;
}

PointerInterface *SeatInterface::pointer() const
{
    return d->pointer.get();
}

static quint32 qtToWaylandButton(Qt::MouseButton button)
{
    static const QHash<Qt::MouseButton, quint32> s_buttons({
        {Qt::LeftButton, BTN_LEFT},
        {Qt::RightButton, BTN_RIGHT},
        {Qt::MiddleButton, BTN_MIDDLE},
        {Qt::ExtraButton1, BTN_BACK}, // note: QtWayland maps BTN_SIDE
        {Qt::ExtraButton2, BTN_FORWARD}, // note: QtWayland maps BTN_EXTRA
        {Qt::ExtraButton3, BTN_TASK}, // note: QtWayland maps BTN_FORWARD
        {Qt::ExtraButton4, BTN_EXTRA}, // note: QtWayland maps BTN_BACK
        {Qt::ExtraButton5, BTN_SIDE}, // note: QtWayland maps BTN_TASK
        {Qt::ExtraButton6, BTN_TASK + 1},
        {Qt::ExtraButton7, BTN_TASK + 2},
        {Qt::ExtraButton8, BTN_TASK + 3},
        {Qt::ExtraButton9, BTN_TASK + 4},
        {Qt::ExtraButton10, BTN_TASK + 5},
        {Qt::ExtraButton11, BTN_TASK + 6},
        {Qt::ExtraButton12, BTN_TASK + 7},
        {Qt::ExtraButton13, BTN_TASK + 8}
        // further mapping not possible, 0x120 is BTN_JOYSTICK
    });
    return s_buttons.value(button, 0);
}

bool SeatInterface::isPointerButtonPressed(Qt::MouseButton button) const
{
    return isPointerButtonPressed(qtToWaylandButton(button));
}

bool SeatInterface::isPointerButtonPressed(quint32 button) const
{
    return d->globalPointer.grabs.contains(button);
}

void SeatInterface::notifyPointerAxis(Qt::Orientation orientation, qreal delta, qint32 deltaV120, PointerAxisSource source, PointerAxisRelativeDirection direction)
{
    if (!d->pointer) {
        return;
    }
    if (d->drag.mode == SeatInterfacePrivate::Drag::Mode::Pointer) {
        // ignore
        return;
    }
    d->pointer->sendAxis(orientation, delta, deltaV120, source, direction);
}

void SeatInterface::notifyPointerButton(Qt::MouseButton button, PointerButtonState state)
{
    const quint32 nativeButton = qtToWaylandButton(button);
    if (nativeButton == 0) {
        return;
    }
    notifyPointerButton(nativeButton, state);
}

void SeatInterface::notifyPointerButton(quint32 button, PointerButtonState state)
{
    if (!d->pointer) {
        return;
    }
    const quint32 serial = d->display->nextSerial();

    if (state == PointerButtonState::Pressed) {
        d->globalPointer.grabs[button] = serial;
        if (d->drag.mode == SeatInterfacePrivate::Drag::Mode::Pointer) {
            // ignore
            return;
        }
    } else {
        std::optional<quint32> implicitGrabSerial;
        if (auto it = d->globalPointer.grabs.find(button); it != d->globalPointer.grabs.end()) {
            implicitGrabSerial = it.value();
            d->globalPointer.grabs.erase(it);
        }
        if (d->drag.mode == SeatInterfacePrivate::Drag::Mode::Pointer) {
            if (d->drag.dragImplicitGrabSerial != implicitGrabSerial) {
                // not our drag button - ignore
                return;
            }

            SurfaceInterface *focusedSurface = focusedPointerSurface();
            if (focusedSurface && !d->dragInhibitsPointer(focusedSurface)) {
                d->pointer->sendButton(button, state, serial);
            }
            d->endDrag();
            return;
        }
    }

    d->pointer->sendButton(button, state, serial);
}

void SeatInterface::notifyPointerFrame()
{
    if (!d->pointer) {
        return;
    }
    SurfaceInterface *focusedSurface = focusedPointerSurface();
    if (focusedSurface && d->dragInhibitsPointer(focusedSurface)) {
        return;
    }
    d->pointer->sendFrame();
}

quint32 SeatInterface::pointerButtonSerial(Qt::MouseButton button) const
{
    return pointerButtonSerial(qtToWaylandButton(button));
}

quint32 SeatInterface::pointerButtonSerial(quint32 button) const
{
    auto it = d->globalPointer.grabs.constFind(button);
    if (it == d->globalPointer.grabs.constEnd()) {
        return 0;
    }
    return it.value();
}

void SeatInterface::relativePointerMotion(const QPointF &delta, const QPointF &deltaNonAccelerated, std::chrono::microseconds time)
{
    if (!d->pointer) {
        return;
    }

    auto relativePointer = RelativePointerV1Interface::get(pointer());
    if (relativePointer) {
        relativePointer->sendRelativeMotion(delta, deltaNonAccelerated, time);
    }
}

void SeatInterface::startPointerSwipeGesture(quint32 fingerCount)
{
    if (!d->pointer) {
        return;
    }

    auto swipeGesture = PointerSwipeGestureV1Interface::get(pointer());
    if (swipeGesture) {
        swipeGesture->sendBegin(d->display->nextSerial(), fingerCount);
    }
}

void SeatInterface::updatePointerSwipeGesture(const QPointF &delta)
{
    if (!d->pointer) {
        return;
    }

    auto swipeGesture = PointerSwipeGestureV1Interface::get(pointer());
    if (swipeGesture) {
        swipeGesture->sendUpdate(delta);
    }
}

void SeatInterface::endPointerSwipeGesture()
{
    if (!d->pointer) {
        return;
    }

    auto swipeGesture = PointerSwipeGestureV1Interface::get(pointer());
    if (swipeGesture) {
        swipeGesture->sendEnd(d->display->nextSerial());
    }
}

void SeatInterface::cancelPointerSwipeGesture()
{
    if (!d->pointer) {
        return;
    }

    auto swipeGesture = PointerSwipeGestureV1Interface::get(pointer());
    if (swipeGesture) {
        swipeGesture->sendCancel(d->display->nextSerial());
    }
}

void SeatInterface::startPointerPinchGesture(quint32 fingerCount)
{
    if (!d->pointer) {
        return;
    }

    auto pinchGesture = PointerPinchGestureV1Interface::get(pointer());
    if (pinchGesture) {
        pinchGesture->sendBegin(d->display->nextSerial(), fingerCount);
    }
}

void SeatInterface::updatePointerPinchGesture(const QPointF &delta, qreal scale, qreal rotation)
{
    if (!d->pointer) {
        return;
    }

    auto pinchGesture = PointerPinchGestureV1Interface::get(pointer());
    if (pinchGesture) {
        pinchGesture->sendUpdate(delta, scale, rotation);
    }
}

void SeatInterface::endPointerPinchGesture()
{
    if (!d->pointer) {
        return;
    }

    auto pinchGesture = PointerPinchGestureV1Interface::get(pointer());
    if (pinchGesture) {
        pinchGesture->sendEnd(d->display->nextSerial());
    }
}

void SeatInterface::cancelPointerPinchGesture()
{
    if (!d->pointer) {
        return;
    }

    auto pinchGesture = PointerPinchGestureV1Interface::get(pointer());
    if (pinchGesture) {
        pinchGesture->sendCancel(d->display->nextSerial());
    }
}

void SeatInterface::startPointerHoldGesture(quint32 fingerCount)
{
    if (!d->pointer) {
        return;
    }

    auto holdGesture = PointerHoldGestureV1Interface::get(pointer());
    if (holdGesture) {
        holdGesture->sendBegin(d->display->nextSerial(), fingerCount);
    }
}

void SeatInterface::endPointerHoldGesture()
{
    if (!d->pointer) {
        return;
    }

    auto holdGesture = PointerHoldGestureV1Interface::get(pointer());
    if (holdGesture) {
        holdGesture->sendEnd(d->display->nextSerial());
    }
}

void SeatInterface::cancelPointerHoldGesture()
{
    if (!d->pointer) {
        return;
    }

    auto holdGesture = PointerHoldGestureV1Interface::get(pointer());
    if (holdGesture) {
        holdGesture->sendCancel(d->display->nextSerial());
    }
}

SurfaceInterface *SeatInterface::focusedKeyboardSurface() const
{
    return d->globalKeyboard.focus.surface;
}

void SeatInterface::setFocusedKeyboardSurface(SurfaceInterface *surface)
{
    if (!d->keyboard) {
        return;
    }

    Q_EMIT focusedKeyboardSurfaceAboutToChange(surface);
    const quint32 serial = d->display->nextSerial();

    if (d->globalKeyboard.focus.surface) {
        disconnect(d->globalKeyboard.focus.destroyConnection);
    }
    d->globalKeyboard.focus = SeatInterfacePrivate::Keyboard::Focus();
    d->globalKeyboard.focus.surface = surface;

    d->keyboard->setFocusedSurface(surface, serial);

    if (d->globalKeyboard.focus.surface) {
        d->globalKeyboard.focus.destroyConnection = connect(surface, &QObject::destroyed, this, [this]() {
            d->globalKeyboard.focus = SeatInterfacePrivate::Keyboard::Focus();
        });
        d->globalKeyboard.focus.serial = serial;
        // selection?
        const QList<DataDeviceInterface *> dataDevices = d->dataDevicesForSurface(surface);
        d->globalKeyboard.focus.selections = dataDevices;
        for (auto dataDevice : dataDevices) {
            dataDevice->sendSelection(d->currentSelection);
        }
        // primary selection
        QList<PrimarySelectionDeviceV1Interface *> primarySelectionDevices;
        for (auto it = d->primarySelectionDevices.constBegin(); it != d->primarySelectionDevices.constEnd(); ++it) {
            if ((*it)->client() == *surface->client()) {
                primarySelectionDevices << *it;
            }
        }

        d->globalKeyboard.focus.primarySelections = primarySelectionDevices;
        for (auto primaryDataDevice : primarySelectionDevices) {
            primaryDataDevice->sendSelection(d->currentPrimarySelection);
        }
    }

    // focused text input surface follows keyboard
    if (hasKeyboard()) {
        setFocusedTextInputSurface(surface);
    }
}

KeyboardInterface *SeatInterface::keyboard() const
{
    return d->keyboard.get();
}

void SeatInterface::notifyKeyboardKey(quint32 keyCode, KeyboardKeyState state)
{
    if (!d->keyboard) {
        return;
    }
    d->keyboard->sendKey(keyCode, state);
}

void SeatInterface::notifyKeyboardModifiers(quint32 depressed, quint32 latched, quint32 locked, quint32 group)
{
    if (!d->keyboard) {
        return;
    }
    d->keyboard->sendModifiers(depressed, latched, locked, group);
}

void SeatInterface::notifyTouchCancel()
{
    if (!d->touch) {
        return;
    }
    for (auto it = d->globalTouch.focus.begin(), itEnd = d->globalTouch.focus.end(); it != itEnd;) {
        d->touch->sendCancel(it->first);
        it = d->globalTouch.focus.erase(it);
    }

    if (d->drag.mode == SeatInterfacePrivate::Drag::Mode::Touch) {
        // cancel the drag, don't drop. serial does not matter
        d->cancelDrag();
    }
    d->globalTouch.ids.clear();
}

bool SeatInterface::isSurfaceTouched(SurfaceInterface *surface) const
{
    return d->globalTouch.focus.contains(surface->mainSurface());
}

bool SeatInterface::isTouchSequence() const
{
    return !d->globalTouch.ids.empty();
}

TouchInterface *SeatInterface::touch() const
{
    return d->touch.get();
}

QPointF SeatInterface::firstTouchPointPosition(SurfaceInterface *surface) const
{
    const auto it = d->globalTouch.focus.find(surface);
    if (it == d->globalTouch.focus.end()) {
        qCWarning(KWIN_CORE) << "Requested a first touch on a surface that isn't touched" << surface;
        return {};
    }
    return it->second->firstTouchPos;
}

void TouchPoint::setSurfacePosition(const QPointF &surfacePosition)
{
    auto interaction = seat->d->globalTouch.focus.find(surface);
    Q_ASSERT(interaction != seat->d->globalTouch.focus.end());
    interaction->second->offset = surfacePosition;
    interaction->second->transformation = QMatrix4x4();
    interaction->second->transformation.translate(-surfacePosition.x(), -surfacePosition.y());
}

void SeatInterface::discardSurfaceTouches(SurfaceInterface *surface)
{
    if (!surface) {
        return;
    }
    auto it = d->globalTouch.focus.find(surface);
    if (it == d->globalTouch.focus.end()) {
        return;
    }

    for (auto itId = d->globalTouch.ids.begin(); itId != d->globalTouch.ids.end();) {
        if (itId->second->surface == surface) {
            itId = d->globalTouch.ids.erase(itId);
        } else {
            ++itId;
        }
    }
    d->touch->sendCancel(surface);
    d->globalTouch.focus.erase(it);
}

TouchPoint *SeatInterface::notifyTouchDown(SurfaceInterface *surface, const QPointF &surfacePosition, qint32 id, const QPointF &globalPosition)
{
    Q_ASSERT(!d->globalTouch.ids.contains(id));
    if (!d->touch || !surface) {
        return {};
    }

    auto it = d->globalTouch.focus.find(surface);
    if (it == d->globalTouch.focus.end()) {
        d->globalTouch.focus[surface] = std::make_unique<SeatInterfacePrivate::Touch::Interaction>();
        it = d->globalTouch.focus.find(surface);

        it->second->firstTouchPos = globalPosition;
        it->second->destroyConnection = QObject::connect(surface, &SurfaceInterface::aboutToBeDestroyed, this, [this, surface]() {
            discardSurfaceTouches(surface);
        });
    }
    it->second->refs++;
    it->second->offset = surfacePosition;
    it->second->transformation = QMatrix4x4();
    it->second->transformation.translate(-surfacePosition.x(), -surfacePosition.y());

    auto pos = globalPosition - it->second->offset;
    SurfaceInterface *effectiveTouchedSurface = mapToSurfaceInPosition(surface, pos);
    const quint32 serial = display()->nextSerial();
    d->touch->sendDown(effectiveTouchedSurface, id, serial, pos);

    if (id == 0 && hasPointer() && surface) {
        TouchInterfacePrivate *touchPrivate = TouchInterfacePrivate::get(d->touch.get());
        if (!touchPrivate->hasTouchesForClient(effectiveTouchedSurface->client())) {
            // If the client did not bind the touch interface fall back
            // to at least emulating touch through pointer events.
            d->pointer->sendEnter(effectiveTouchedSurface, pos, serial);
            d->pointer->sendMotion(pos);
            d->pointer->sendFrame();
        }
    }

    auto tp = std::make_unique<TouchPoint>(serial, surface, this);
    auto r = tp.get();
    d->globalTouch.ids[id] = std::move(tp);
    return r;
}

void SeatInterface::notifyTouchMotion(qint32 id, const QPointF &globalPosition)
{
    if (!d->touch) {
        return;
    }
    auto itTouch = d->globalTouch.ids.find(id);
    if (itTouch == d->globalTouch.ids.cend()) {
        // This can happen in cases where the interaction started while the device was asleep
        qCWarning(KWIN_CORE) << "Detected a touch move that never has been down, discarding";
        return;
    }
    Q_ASSERT(itTouch->second->surface);

    auto interaction = d->globalTouch.focus.find(itTouch->second->surface);
    if (interaction == d->globalTouch.focus.end()) {
        qCDebug(KWIN_CORE) << "Surface not there, discarding";
        return;
    }
    auto pos = globalPosition - interaction->second->offset;
    SurfaceInterface *effectiveTouchedSurface = mapToSurfaceInPosition(d->globalTouch.ids[id]->surface, pos);

    if (isDragTouch()) {
        // handled by DataDevice
    } else {
        d->touch->sendMotion(effectiveTouchedSurface, id, pos);
    }

    if (id == 0) {
        interaction->second->firstTouchPos = globalPosition;

        if (hasPointer() && itTouch->second->surface) {
            TouchInterfacePrivate *touchPrivate = TouchInterfacePrivate::get(d->touch.get());
            if (!touchPrivate->hasTouchesForClient(itTouch->second->surface->client())) {
                // Client did not bind touch, fall back to emulating with pointer events.
                d->pointer->sendMotion(pos);
                d->pointer->sendFrame();
            }
        }
    }
    Q_EMIT touchMoved(id, itTouch->second->serial, globalPosition);
}

void SeatInterface::notifyTouchUp(qint32 id)
{
    if (!d->touch) {
        return;
    }

    auto itTouch = d->globalTouch.ids.find(id);
    if (itTouch == d->globalTouch.ids.end()) {
        // This can happen in cases where the interaction started while the device was asleep
        qCWarning(KWIN_CORE) << "Detected a touch that never started, discarding";
        return;
    }
    const qint32 serial = d->display->nextSerial();
    if (d->drag.mode == SeatInterfacePrivate::Drag::Mode::Touch && d->drag.dragImplicitGrabSerial == itTouch->second->serial) {
        // the implicitly grabbing touch point has been upped
        d->endDrag();
    }

    auto client = itTouch->second->surface->client();
    d->touch->sendUp(client, id, serial);
    if (id == 0 && hasPointer() && client) {
        TouchInterfacePrivate *touchPrivate = TouchInterfacePrivate::get(d->touch.get());
        if (!touchPrivate->hasTouchesForClient(client)) {
            // Client did not bind touch, fall back to emulating with pointer events.
            const quint32 serial = display()->nextSerial();
            d->pointer->sendButton(BTN_LEFT, PointerButtonState::Released, serial);
            d->pointer->sendFrame();
        }
    }

    auto it = d->globalTouch.focus.find(itTouch->second->surface);
    Q_ASSERT(it != d->globalTouch.focus.end());
    it->second->refs--;
    if (it->second->refs == 0) {
        d->globalTouch.focus.erase(it);
    }
    d->globalTouch.ids.erase(itTouch);
}

void SeatInterface::notifyTouchFrame()
{
    if (!d->touch) {
        return;
    }
    d->touch->sendFrame();
}

bool SeatInterface::hasImplicitTouchGrab(quint32 serial) const
{
    return std::ranges::any_of(std::as_const(d->globalTouch.ids), [serial](const auto &x) {
        return x.second->serial == serial;
    });
}

bool SeatInterface::isDrag() const
{
    return d->drag.mode != SeatInterfacePrivate::Drag::Mode::None;
}

bool SeatInterface::isDragPointer() const
{
    return d->drag.mode == SeatInterfacePrivate::Drag::Mode::Pointer;
}

bool SeatInterface::isDragTouch() const
{
    return d->drag.mode == SeatInterfacePrivate::Drag::Mode::Touch;
}

bool SeatInterface::hasImplicitPointerGrab(quint32 serial) const
{
    return std::any_of(d->globalPointer.grabs.constBegin(), d->globalPointer.grabs.constEnd(), [serial](const auto &grab) {
        return grab == serial;
    });
}

QMatrix4x4 SeatInterface::dragSurfaceTransformation() const
{
    return d->drag.transformation;
}

SurfaceInterface *SeatInterface::dragSurface() const
{
    return d->drag.surface;
}

AbstractDataSource *SeatInterface::dragSource() const
{
    return d->drag.source;
}

XdgToplevelDragV1Interface *SeatInterface::xdgTopleveldrag() const
{
    if (auto source = qobject_cast<DataSourceInterface *>(d->drag.source)) {
        return source->xdgToplevelDrag();
    }
    return nullptr;
}

void SeatInterface::setFocusedTextInputSurface(SurfaceInterface *surface)
{
    const quint32 serial = d->display->nextSerial();

    if (d->focusedTextInputSurface == surface) {
        return;
    }

    if (d->focusedTextInputSurface) {
        disconnect(d->focusedSurfaceDestroyConnection);
        d->textInputV1->d->sendLeave(d->focusedTextInputSurface);
        d->textInputV2->d->sendLeave(serial, d->focusedTextInputSurface);
        d->textInputV3->d->sendLeave(d->focusedTextInputSurface);
    }
    d->focusedTextInputSurface = surface;

    if (surface) {
        d->focusedSurfaceDestroyConnection = connect(surface, &SurfaceInterface::aboutToBeDestroyed, this, [this] {
            setFocusedTextInputSurface(nullptr);
        });
        d->textInputV1->d->sendEnter(surface);
        d->textInputV2->d->sendEnter(surface, serial);
        d->textInputV3->d->sendEnter(surface);
    }

    Q_EMIT focusedTextInputSurfaceChanged();
}

SurfaceInterface *SeatInterface::focusedTextInputSurface() const
{
    return d->focusedTextInputSurface;
}

TextInputV1Interface *SeatInterface::textInputV1() const
{
    return d->textInputV1;
}

TextInputV2Interface *SeatInterface::textInputV2() const
{
    return d->textInputV2;
}

TextInputV3Interface *SeatInterface::textInputV3() const
{
    return d->textInputV3;
}
AbstractDataSource *SeatInterface::selection() const
{
    return d->currentSelection;
}

void SeatInterface::setSelection(AbstractDataSource *selection, quint32 serial)
{
    if (d->currentSelection == selection) {
        return;
    }

    if (d->currentSelection) {
        d->currentSelection->cancel();
        disconnect(d->currentSelection, nullptr, this, nullptr);
    }

    if (selection) {
        auto cleanup = [this, serial]() {
            setSelection(nullptr, serial);
        };
        connect(selection, &AbstractDataSource::aboutToBeDestroyed, this, cleanup);
    }

    d->currentSelection = selection;
    d->currentSelectionSerial = serial;

    for (auto focussedSelection : std::as_const(d->globalKeyboard.focus.selections)) {
        focussedSelection->sendSelection(selection);
    }

    for (auto control : std::as_const(d->dataControlDevices)) {
        control->sendSelection(selection);
    }

    Q_EMIT selectionChanged(selection);
}

AbstractDataSource *SeatInterface::primarySelection() const
{
    return d->currentPrimarySelection;
}

void SeatInterface::setPrimarySelection(AbstractDataSource *selection, quint32 serial)
{
    if (d->currentPrimarySelection == selection) {
        return;
    }
    if (d->currentPrimarySelection) {
        d->currentPrimarySelection->cancel();
        disconnect(d->currentPrimarySelection, nullptr, this, nullptr);
    }

    if (selection) {
        auto cleanup = [this, serial]() {
            setPrimarySelection(nullptr, serial);
        };
        connect(selection, &AbstractDataSource::aboutToBeDestroyed, this, cleanup);
    }

    d->currentPrimarySelection = selection;
    d->currentPrimarySelectionSerial = serial;

    for (auto focussedSelection : std::as_const(d->globalKeyboard.focus.primarySelections)) {
        focussedSelection->sendSelection(selection);
    }
    for (auto control : std::as_const(d->dataControlDevices)) {
        control->sendPrimarySelection(selection);
    }

    Q_EMIT primarySelectionChanged(selection);
}

void SeatInterface::startDrag(AbstractDataSource *dragSource, SurfaceInterface *originSurface, int dragSerial, DragAndDropIcon *dragIcon)
{
    if (d->drag.mode != SeatInterfacePrivate::Drag::Mode::None) {
        return;
    }
    originSurface = originSurface->mainSurface();

    if (hasImplicitPointerGrab(dragSerial)) {
        d->drag.mode = SeatInterfacePrivate::Drag::Mode::Pointer;
        d->drag.transformation = d->globalPointer.focus.transformation;
    } else if (hasImplicitTouchGrab(dragSerial) && d->globalTouch.focus.contains(originSurface)) {
        d->drag.mode = SeatInterfacePrivate::Drag::Mode::Touch;
        // identify touch id
        d->drag.transformation = d->globalTouch.focus[originSurface]->transformation;
    } else {
        // no implicit grab, abort drag
        return;
    }

    d->drag.dragImplicitGrabSerial = dragSerial;

    // set initial drag target to ourself
    d->drag.surface = originSurface;

    d->drag.source = dragSource;
    if (dragSource) {
        d->drag.dragSourceDestroyConnection = QObject::connect(dragSource, &AbstractDataSource::aboutToBeDestroyed, this, [this] {
            d->cancelDrag();
        });
    }
    d->drag.dragIcon = dragIcon;

    if (!d->dataDevicesForSurface(originSurface).isEmpty()) {
        d->drag.target = d->dataDevicesForSurface(originSurface)[0];
    }
    if (d->drag.target) {
        if (d->dragInhibitsPointer(originSurface)) {
            notifyPointerLeave();
        }
        d->drag.target->updateDragTarget(originSurface, display()->nextSerial());
    }
    Q_EMIT dragStarted();
    Q_EMIT dragSurfaceChanged();
}

DragAndDropIcon *SeatInterface::dragIcon() const
{
    return d->drag.dragIcon;
}
}

#include "moc_seat.cpp"
