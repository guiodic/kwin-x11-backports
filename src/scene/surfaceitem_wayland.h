/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "scene/surfaceitem.h"

#include <unordered_map>

namespace KWin
{

class GraphicsBuffer;
class SubSurfaceInterface;
class SurfaceInterface;

/**
 * The SurfaceItemWayland class represents a Wayland surface in the scene.
 */
class KWIN_EXPORT SurfaceItemWayland : public SurfaceItem
{
    Q_OBJECT

public:
    explicit SurfaceItemWayland(SurfaceInterface *surface, Item *parent = nullptr);

    QList<QRectF> shape() const override;
    QRegion opaque() const override;
    ContentType contentType() const override;
    void setScanoutHint(DrmDevice *device, const QHash<uint32_t, QList<uint64_t>> &drmFormats) override;
    void freeze() override;

    SurfaceInterface *surface() const;

private Q_SLOTS:
    void handleSurfaceCommitted();
    void handleSurfaceSizeChanged();
    void handleBufferSizeChanged();
    void handleBufferSourceBoxChanged();
    void handleBufferTransformChanged();

    void handleChildSubSurfaceRemoved(SubSurfaceInterface *child);
    void handleChildSubSurfacesChanged();
    void handleSubSurfacePositionChanged();
    void handleSubSurfaceMappedChanged();
    void handleColorDescriptionChanged();
    void handlePresentationModeHintChanged();
    void handleReleasePointChanged();
    void handleAlphaMultiplierChanged();

protected:
    std::unique_ptr<SurfacePixmap> createPixmap() override;

private:
    SurfaceItemWayland *getOrCreateSubSurfaceItem(SubSurfaceInterface *s);

    QPointer<SurfaceInterface> m_surface;
    struct ScanoutFeedback
    {
        DrmDevice *device = nullptr;
        QHash<uint32_t, QList<uint64_t>> formats;
    };
    std::optional<ScanoutFeedback> m_scanoutFeedback;
    std::unordered_map<SubSurfaceInterface *, std::unique_ptr<SurfaceItemWayland>> m_subsurfaces;
};

class KWIN_EXPORT SurfacePixmapWayland final : public SurfacePixmap
{
    Q_OBJECT

public:
    explicit SurfacePixmapWayland(SurfaceItemWayland *item, QObject *parent = nullptr);

    void create() override;
    void update() override;
    bool isValid() const override;

private:
    SurfaceItemWayland *m_item;
};

} // namespace KWin
