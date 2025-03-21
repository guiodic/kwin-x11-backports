/*
    SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import Qt5Compat.GraphicalEffects
import QtQuick.Layouts
import org.kde.kwin as KWinComponents
import org.kde.kwin_x11.private.effects
import org.kde.kirigami as Kirigami

Rectangle {
    id: handle

    required property QtObject tile

    required property int corner

    z: 2

    implicitWidth: Kirigami.Units.gridUnit
    implicitHeight: Kirigami.Units.gridUnit

    radius: Kirigami.Units.cornerRadius
    color: Kirigami.Theme.highlightColor
    opacity: hoverHandler.hovered || dragHandler.active ? 0.4 : 0
    visible: tile &&
        (tile.layoutDirection === KWinComponents.Tile.Floating ||
         (corner === Qt.TopLeftCorner || corner === Qt.BottomLeftCorner ? tile.relativeGeometry.x > 0 : true) &&
         (corner === Qt.TopLeftCorner || corner === Qt.TopRightCorner ? tile.relativeGeometry.y > 0 : true) &&
         (corner === Qt.TopRightCorner || corner === Qt.BottomRightCorner ? tile.relativeGeometry.x +  tile.relativeGeometry.width < 1 : true) &&
         (corner === Qt.BottomLeftCorner || corner === Qt.BottomRightCorner ? tile.relativeGeometry.y + tile.relativeGeometry.height < 1 : true))

    HoverHandler {
        id: hoverHandler
        grabPermissions: PointerHandler.TakeOverForbidden | PointerHandler.CanTakeOverFromAnything
        cursorShape: {
            switch (handle.corner) {
            case Qt.TopLeftCorner:
                return Qt.SizeFDiagCursor;
            case Qt.TopRightCorner:
                return Qt.SizeBDiagCursor;
            case Qt.BottomLeftCorner:
                return Qt.SizeBDiagCursor;
            case Qt.BottomRightCorner:
                return Qt.SizeFDiagCursor;
            }
        }
    }

    DragHandler {
        id: dragHandler
        target: null
        property point oldPoint: Qt.point(0, 0)
        property point dragPoint: centroid.scenePosition
        dragThreshold: 0
        grabPermissions: PointerHandler.TakeOverForbidden | PointerHandler.CanTakeOverFromAnything
        onActiveChanged: {
            if (active) {
                oldPoint = dragPoint;
            }
        }
        onDragPointChanged: {
            if (!active) {
                return;
            }
            switch (handle.corner) {
            case Qt.TopLeftCorner:
                tile.resizeByPixels(dragPoint.x - oldPoint.x, Qt.LeftEdge);
                tile.resizeByPixels(dragPoint.y - oldPoint.y, Qt.TopEdge);
                break;
            case Qt.TopRightCorner:
                tile.resizeByPixels(dragPoint.x - oldPoint.x, Qt.RightEdge);
                tile.resizeByPixels(dragPoint.y - oldPoint.y, Qt.TopEdge);
                break;
            case Qt.BottomLeftCorner:
                tile.resizeByPixels(dragPoint.x - oldPoint.x, Qt.LeftEdge);
                tile.resizeByPixels(dragPoint.y - oldPoint.y, Qt.BottomEdge);
                break;
            case Qt.BottomRightCorner:
                tile.resizeByPixels(dragPoint.x - oldPoint.x, Qt.RightEdge);
                tile.resizeByPixels(dragPoint.y - oldPoint.y, Qt.BottomEdge);
                break;
            }

            oldPoint = dragPoint;
        }
    }
}
