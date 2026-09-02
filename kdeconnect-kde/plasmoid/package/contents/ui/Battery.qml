/**
 * SPDX-FileCopyrightText: 2014 Samoilenko Yuri <kinnalru@gmail.com>
 * SPDX-FileCopyrightText: 2024 ivan tkachenko <me@ratijas.tk>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

pragma ComponentBehavior: Bound

import QtQuick

import org.kde.kdeconnect as KDEConnect

QtObject {
    id: root

    required property KDEConnect.DeviceDbusInterface device

    readonly property alias available: checker.available

    readonly property KDEConnect.PluginChecker pluginChecker: KDEConnect.PluginChecker {
        id: checker
        pluginName: "battery"
        device: root.device
    }

    readonly property bool charging: battery?.isCharging ?? false
    readonly property int charge: battery?.charge ?? -1
    readonly property bool hasBattery: battery?.hasBattery ?? false
    readonly property string iconName: battery?.iconName ?? "battery-missing-symbolic"

    readonly property string displayString: {
        if (available && charge > -1) {
            if (charging) {
                return i18n("%1% charging", charge);
            } else {
                return i18n("%1%", charge);
            }
        } else {
            return i18n("No info");
        }
    }

    property KDEConnect.BatteryDbusInterface battery

    onAvailableChanged: {
        if (available) {
            battery = KDEConnect.DeviceBatteryDbusInterfaceFactory.create(device.id());
        } else {
            battery = null;
        }
    }
}
