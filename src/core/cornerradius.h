/*
    SPDX-FileCopyrightText: 2024 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include "kwin_export.h"

#include <QRegion>
#include <QVector4D>

namespace KWin
{

class KWIN_EXPORT CornerRadius
{
public:
    CornerRadius();
    explicit CornerRadius(qreal radius);
    explicit CornerRadius(qreal topLeft, qreal topRight, qreal bottomRight, qreal bottomLeft);

    bool operator<=>(const CornerRadius &other) const = default;

    bool isIdentity() const;

    qreal topLeft() const;
    qreal bottomLeft() const;
    qreal topRight() const;
    qreal bottomRight() const;

    QRegion clip(const QRegion &region, const QRectF &bounds) const;

    CornerRadius scaled(qreal scale) const;
    CornerRadius rounded() const;

    QVector4D toVector() const;

private:
    qreal m_topLeft = 0;
    qreal m_topRight = 0;
    qreal m_bottomRight = 0;
    qreal m_bottomLeft = 0;
};

inline CornerRadius::CornerRadius()
{
}

inline CornerRadius::CornerRadius(qreal radius)
    : m_topLeft(radius)
    , m_topRight(radius)
    , m_bottomRight(radius)
    , m_bottomLeft(radius)
{
}

inline CornerRadius::CornerRadius(qreal topLeft, qreal topRight, qreal bottomRight, qreal bottomLeft)
    : m_topLeft(topLeft)
    , m_topRight(topRight)
    , m_bottomRight(bottomRight)
    , m_bottomLeft(bottomLeft)
{
}

inline bool CornerRadius::isIdentity() const
{
    return qFuzzyIsNull(m_topLeft) && qFuzzyIsNull(m_topRight) && qFuzzyIsNull(m_bottomRight) && qFuzzyIsNull(m_bottomLeft);
}

inline qreal CornerRadius::topLeft() const
{
    return m_topLeft;
}

inline qreal CornerRadius::bottomLeft() const
{
    return m_bottomLeft;
}

inline qreal CornerRadius::topRight() const
{
    return m_topRight;
}

inline qreal CornerRadius::bottomRight() const
{
    return m_bottomRight;
}

inline CornerRadius CornerRadius::scaled(qreal scale) const
{
    return CornerRadius(m_topLeft * scale,
                        m_topRight * scale,
                        m_bottomRight * scale,
                        m_bottomLeft * scale);
}

inline CornerRadius CornerRadius::rounded() const
{
    return CornerRadius(std::round(m_topLeft),
                        std::round(m_topRight),
                        std::round(m_bottomRight),
                        std::round(m_bottomLeft));
}

inline QVector4D CornerRadius::toVector() const
{
    return QVector4D(m_topLeft, m_topRight, m_bottomLeft, m_bottomRight);
}

inline QRegion CornerRadius::clip(const QRegion &region, const QRectF &bounds) const
{
    if (region.isEmpty()) {
        return QRegion();
    }

    QRegion clipped = region;

    if (m_topLeft > 0) {
        clipped = clipped.subtracted(QRectF(0, 0, m_topLeft, m_topLeft).toAlignedRect());
    }

    if (m_topRight > 0) {
        clipped = clipped.subtracted(QRectF(bounds.x() + bounds.width() - m_topRight, 0, m_topRight, m_topRight).toAlignedRect());
    }

    if (m_bottomRight > 0) {
        clipped = clipped.subtracted(QRectF(bounds.x() + bounds.width() - m_bottomRight, bounds.y() + bounds.height() - m_bottomRight, m_bottomRight, m_bottomRight).toAlignedRect());
    }

    if (m_bottomLeft > 0) {
        clipped = clipped.subtracted(QRectF(0, bounds.y() + bounds.height() - m_bottomLeft, m_bottomLeft, m_bottomLeft).toAlignedRect());
    }

    return clipped;
}

} // namespace KWin
