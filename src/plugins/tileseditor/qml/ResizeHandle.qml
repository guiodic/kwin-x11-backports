/*
    SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import org.kde.kwin as KWinComponents
import org.kde.kwin_x11.private.effects
import org.kde.kirigami as Kirigami

Rectangle {
    id: handle

    required property QtObject tile

    required property int edge
    readonly property int orientation: edge === Qt.LeftEdge || edge === Qt.RightEdge ? Qt.Horizontal : Qt.Vertical
    readonly property bool valid: tile !== null && tile.parent !== null && (tile.layoutDirection === KWinComponents.Tile.Floating
        || (orientation === Qt.Horizontal && tile.parent.layoutDirection === KWinComponents.Tile.Horizontal)
        || (orientation === Qt.Vertical && tile.parent.layoutDirection === KWinComponents.Tile.Vertical))

    z: 2

    implicitWidth: Kirigami.Units.smallSpacing * 2
    implicitHeight: Kirigami.Units.smallSpacing * 2

    radius: Kirigami.Units.cornerRadius
    color: Kirigami.Theme.highlightColor
    opacity: hoverHandler.hovered || dragHandler.active ? 0.4 : 0
    visible: valid && (tile.layoutDirection === KWinComponents.Tile.Floating || tile.positionInLayout > 0)

    HoverHandler {
        id: hoverHandler
        cursorShape: orientation == Qt.Horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }

    DragHandler {
        id: dragHandler
        target: null
        property point oldPoint: Qt.point(0, 0)
        property point dragPoint: centroid.scenePosition
        dragThreshold: 0
        onActiveChanged: {
            if (active) {
                oldPoint = dragPoint;
            }
        }
        onDragPointChanged: {
            if (!active) {
                return;
            }
            switch (handle.edge) {
            case Qt.LeftEdge:
            case Qt.RightEdge:
                tile.resizeByPixels(dragPoint.x - oldPoint.x, handle.edge);
                break;
            case Qt.TopEdge:
            case Qt.BottomEdge:
                tile.resizeByPixels(dragPoint.y - oldPoint.y, handle.edge);
                break;
            }

            oldPoint = dragPoint;
        }
    }
}
