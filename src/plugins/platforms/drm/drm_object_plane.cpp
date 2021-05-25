/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_object_plane.h"
#include "drm_buffer.h"
#include "drm_gpu.h"
#include "drm_pointer.h"
#include "logging.h"

namespace KWin
{

DrmPlane::DrmPlane(DrmGpu *gpu, uint32_t planeId)
    : DrmObject(gpu, planeId)
{
}

bool DrmPlane::init()
{
    qCDebug(KWIN_DRM) << "Atomic init for plane:" << id();
    DrmScopedPointer<drmModePlane> p(drmModeGetPlane(gpu()->fd(), id()));

    if (!p) {
        qCWarning(KWIN_DRM) << "Failed to get kernel plane" << id();
        return false;
    }

    m_possibleCrtcs = p->possible_crtcs;

    int count_formats = p->count_formats;
    m_formats.resize(count_formats);
    for (int i = 0; i < count_formats; i++) {
        m_formats[i] = p->formats[i];
    }

    bool success = initProps({
        PropertyDefinition(QByteArrayLiteral("type"), {
            QByteArrayLiteral("Overlay"),
            QByteArrayLiteral("Primary"),
            QByteArrayLiteral("Cursor")}),
        PropertyDefinition(QByteArrayLiteral("SRC_X")),
        PropertyDefinition(QByteArrayLiteral("SRC_Y")),
        PropertyDefinition(QByteArrayLiteral("SRC_W")),
        PropertyDefinition(QByteArrayLiteral("SRC_H")),
        PropertyDefinition(QByteArrayLiteral("CRTC_X")),
        PropertyDefinition(QByteArrayLiteral("CRTC_Y")),
        PropertyDefinition(QByteArrayLiteral("CRTC_W")),
        PropertyDefinition(QByteArrayLiteral("CRTC_H")),
        PropertyDefinition(QByteArrayLiteral("FB_ID")),
        PropertyDefinition(QByteArrayLiteral("CRTC_ID")),
        PropertyDefinition(QByteArrayLiteral("rotation"), {
            QByteArrayLiteral("rotate-0"),
            QByteArrayLiteral("rotate-90"),
            QByteArrayLiteral("rotate-180"),
            QByteArrayLiteral("rotate-270"),
            QByteArrayLiteral("reflect-x"),
            QByteArrayLiteral("reflect-y")}),
        }, DRM_MODE_OBJECT_PLANE
    );
    if (success) {
        m_supportedTransformations = Transformations();
        auto checkSupport = [this] (uint64_t value, Transformation t) {
            if (propHasEnum(PropertyIndex::Rotation, value)) {
                m_supportedTransformations |= t;
            }
        };
        checkSupport(0, Transformation::Rotate0);
        checkSupport(1, Transformation::Rotate90);
        checkSupport(2, Transformation::Rotate180);
        checkSupport(3, Transformation::Rotate270);
        checkSupport(4, Transformation::ReflectX);
        checkSupport(5, Transformation::ReflectY);
    }
    return success;
}

DrmPlane::TypeIndex DrmPlane::type()
{
    auto property = getProp(PropertyIndex::Type);
    if (!property) {
        return TypeIndex::Overlay;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(TypeIndex::Count); i++) {
        if (property->enumMap(i) == property->current()) {
            return TypeIndex(i);
        }
    }
    return TypeIndex::Overlay;
}

void DrmPlane::setNext(const QSharedPointer<DrmBuffer> &b)
{
    m_next = b;
}

bool DrmPlane::setTransformation(Transformations t)
{
    if (m_supportedTransformations & t) {
        return setPending(PropertyIndex::Rotation, t);
    } else {
        return false;
    }
}

DrmPlane::Transformations DrmPlane::transformation()
{
    if (auto property = getProp(PropertyIndex::Rotation)) {
        return Transformations(static_cast<uint32_t>(property->pending()));
    }
    return Transformations(Transformation::Rotate0);
}

void DrmPlane::flipBuffer()
{
    m_current = m_next;
    m_next = nullptr;
}

void DrmPlane::set(const QPoint &srcPos, const QSize &srcSize, const QPoint &dstPos, const QSize &dstSize)
{
    // Src* are in 16.16 fixed point format
    setPending(PropertyIndex::SrcX, srcPos.x() << 16);
    setPending(PropertyIndex::SrcY, srcPos.y() << 16);
    setPending(PropertyIndex::SrcW, srcSize.width() << 16);
    setPending(PropertyIndex::SrcH, srcSize.height() << 16);
    setPending(PropertyIndex::CrtcX, dstPos.x());
    setPending(PropertyIndex::CrtcY, dstPos.y());
    setPending(PropertyIndex::CrtcW, dstSize.width());
    setPending(PropertyIndex::CrtcH, dstSize.height());
}

void DrmPlane::setBuffer(DrmBuffer *buffer)
{
    setPending(PropertyIndex::FbId, buffer ? buffer->bufferId() : 0);
}

bool DrmPlane::needsModeset() const
{
    return getProp(PropertyIndex::CrtcId)->needsCommit() || getProp(PropertyIndex::Rotation)->needsCommit();
}

}
