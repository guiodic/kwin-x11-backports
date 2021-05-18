/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "decorationitem.h"

#include <KDecoration2/Decoration>

namespace KWin
{

DecorationItem::DecorationItem(KDecoration2::Decoration *decoration, Scene::Window *window, Item *parent)
    : Item(window, parent)
{
    AbstractClient *toplevel = window->window();

    connect(toplevel, &AbstractClient::frameGeometryChanged,
            this, &DecorationItem::handleFrameGeometryChanged);

    connect(toplevel, &AbstractClient::screenScaleChanged,
            this, &DecorationItem::discardQuads);
    connect(decoration, &KDecoration2::Decoration::bordersChanged,
            this, &DecorationItem::discardQuads);

    setSize(toplevel->size());
}

void DecorationItem::handleFrameGeometryChanged()
{
    setSize(window()->size());
}

} // namespace KWin
