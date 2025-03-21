/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Valerio Pilo <vpilo@coldshock.net>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
import QtQuick

import org.kde.kcmutils as KCM
import org.kde.kirigami 2.20 as Kirigami
import org.kde.kwin_x11.private.kdecoration as KDecoration

KCM.GridView {
    function updateDecoration(item, marginTopLeft, marginBottomRight) {
        const mainMargin = Kirigami.Units.largeSpacing;
        const shd = item.shadow;
        item.anchors.leftMargin   = mainMargin + marginTopLeft     - (shd ? shd.paddingLeft   : 0);
        item.anchors.rightMargin  = mainMargin + marginBottomRight - (shd ? shd.paddingRight  : 0);
        item.anchors.topMargin    = mainMargin + marginTopLeft     - (shd ? shd.paddingTop    : 0);
        item.anchors.bottomMargin = mainMargin + marginBottomRight - (shd ? shd.paddingBottom : 0);
    }

    view.model: kcm.themesModel
    view.currentIndex: kcm.theme
    view.onContentHeightChanged: view.positionViewAtIndex(view.currentIndex, GridView.Visible)

    view.implicitCellWidth: Kirigami.Units.gridUnit * 18

    framedView: false

    view.delegate: KCM.GridDelegate {
        id: delegate
        text: model.display

        thumbnailAvailable: true
        thumbnail: Rectangle {
            anchors.fill: parent
            color: palette.base
            clip: true

            KDecoration.Bridge {
                id: bridgeItem
                plugin: model.plugin
                theme: model.theme
                kcmoduleName: model.kcmoduleName
            }
            KDecoration.Settings {
                id: settingsItem
                bridge: bridgeItem.bridge
                Component.onCompleted: {
                    settingsItem.borderSizesIndex = kcm.borderSize;
                }
            }
            KDecoration.Decoration {
                id: inactivePreview
                bridge: bridgeItem.bridge
                settings: settingsItem
                anchors.fill: parent
                onShadowChanged: updateDecoration(inactivePreview, 0, client.decoration.titleBar.height)
                Component.onCompleted: {
                    client.active = false;
                    client.caption = model.display;
                    updateDecoration(inactivePreview, 0, client.decoration.titleBar.height);
                }
            }
            KDecoration.Decoration {
                id: activePreview
                bridge: bridgeItem.bridge
                settings: settingsItem
                anchors.fill: parent
                onShadowChanged: updateDecoration(activePreview, client.decoration.titleBar.height, 0)
                Component.onCompleted: {
                    client.active = true;
                    client.caption = model.display;
                    updateDecoration(activePreview, client.decoration.titleBar.height, 0);
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: delegate.clicked()
                onDoubleClicked: delegate.doubleClicked()
            }
            Connections {
                target: kcm
                function onBorderSizeChanged() {
                    settingsItem.borderSizesIndex = kcm.borderSize;
                }
            }
        }
        actions: [
            Kirigami.Action {
                icon.name: "edit-entry"
                tooltip: i18n("Edit %1 Theme…", model.display)
                enabled: model.configureable
                onTriggered: {
                    kcm.theme = index;
                    view.currentIndex = index;
                    bridgeItem.bridge.configure(delegate);
                }
            }
        ]

        onClicked: {
            kcm.theme = index;
            view.currentIndex = index;
        }
        onDoubleClicked: {
            kcm.save();
        }
    }
    Connections {
        target: kcm
        function onThemeChanged() {
            view.currentIndex = kcm.theme;
        }
    }
}
