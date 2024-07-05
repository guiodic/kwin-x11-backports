/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include "core/colorpipeline.h"

#include <drm.h>
#include <memory>

namespace KWin
{

class DrmBlob;
class DrmProperty;
class DrmAtomicCommit;

class DrmAbstractColorOp
{
public:
    explicit DrmAbstractColorOp(DrmAbstractColorOp *next);
    virtual ~DrmAbstractColorOp();

    bool matchPipeline(DrmAtomicCommit *commit, const ColorPipeline &pipeline);
    virtual bool canBeUsedFor(const ColorOp &op) = 0;
    struct Scaling
    {
        double offset = 0;
        double scale = 1;
    };
    virtual void program(DrmAtomicCommit *commit, const ColorOp &op, Scaling inputScale, Scaling outputScale) = 0;
    virtual void bypass(DrmAtomicCommit *commit) = 0;

    DrmAbstractColorOp *next() const;

protected:
    DrmAbstractColorOp *m_next = nullptr;

    std::optional<ColorPipeline> m_cachedPipeline;
    std::unique_ptr<DrmAtomicCommit> m_cache;
};

class LegacyLutColorOp : public DrmAbstractColorOp
{
public:
    explicit LegacyLutColorOp(DrmAbstractColorOp *next, DrmProperty *prop, uint32_t maxSize);

    bool canBeUsedFor(const ColorOp &op) override;
    void program(DrmAtomicCommit *commit, const ColorOp &op, Scaling inputScale, Scaling outputScale) override;
    void bypass(DrmAtomicCommit *commit) override;

private:
    DrmProperty *const m_prop;
    const uint32_t m_maxSize;
    QList<drm_color_lut> m_components;
};

class LegacyMatrixColorOp : public DrmAbstractColorOp
{
public:
    explicit LegacyMatrixColorOp(DrmAbstractColorOp *next, DrmProperty *prop);

    bool canBeUsedFor(const ColorOp &op) override;
    void program(DrmAtomicCommit *commit, const ColorOp &op, Scaling inputScale, Scaling outputScale) override;
    void bypass(DrmAtomicCommit *commit) override;

private:
    DrmProperty *const m_prop;
};

}
