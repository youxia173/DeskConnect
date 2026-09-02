/**
 * SPDX-FileCopyrightText: 2026 Aleix Pol i Gonzalez <aleixpol@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kdeconnect

Kirigami.ScrollablePage {
    property string device

    Kirigami.FormLayout {
        KdeConnectPluginConfig {
            id: config
            deviceId: device
            pluginName: "kdeconnect_presenter"
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18nc("@action:label", "Display:")
            readonly property var sources: [
                { name: "PresenterRedDot.qml", display: i18nd("kdeconnect-plugins", "Red Dot") },
                { name: "PresenterElectric.qml", display: i18nd("kdeconnect-plugins", "Electric") },
            ]
            model: sources
            textRole: "display"
            currentIndex: {
                const currentName = config.getString("current", "")
                return Math.max(sources.findIndex(name => name === currentName), 0)
            }
            onActivated: index => config.set("current", sources[index].name)
        }
    }
}
