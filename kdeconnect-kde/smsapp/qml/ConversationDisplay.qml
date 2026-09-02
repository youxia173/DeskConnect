/**
 * SPDX-FileCopyrightText: 2018 Aleix Pol Gonzalez <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2018 Nicolas Fella <nicolas.fella@gmx.de>
 * SPDX-FileCopyrightText: 2018 Simon Redman <simon@ergotech.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kdeconnect.sms

Kirigami.ScrollablePage
{
    title: SmsHelper.getTitleForAddresses(addresses)
    id: page

    property bool deviceConnected
    property string conversationId
    property bool isMultitarget
    property string initialMessage
    property string invalidId: "-1"

    property var addresses

    ListView {
        id: viewport
        verticalLayoutDirection: ListView.BottomToTop
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        model: ConversationModel {
            id: conversationModel
            deviceId: AppData.deviceId
            threadId: page.conversationId
            addressList: page.addresses
            onGotNewMessage: {
                // The distance in viewports heights to the end of the block of messages
                // e.g if normalizedDistance is 0.5, it means that the we have to scroll
                // half a viewport down to reach the end
                let normalizedDistance = (1 - viewport.visibleArea.yPosition) / viewport.visibleArea.heightRatio - 1;
                // If the new message is less then a quarter of the viewport away,
                // scroll down to it
                //
                // This is a little bit of a hack, because of the following scenario:
                //      We are close to the bottom of the list, and a new message is added
                //          - The ListView will shift everything up, by the height of the message
                //              as it lays items out bottom to top
                //          - The user experiences all messages shifted up, but not enough
                //              to reach the new messages
                //  I didn't actually figure out what is the zone in which the messages are loaded
                //  outside of the visible range, but 0.25 of the viewport height
                //  seems to be working across the ranges I tested
                if (normalizedDistance < 0.25) {
                    viewport.positionViewAtBeginning();
                }
            }
        }
        spacing: Kirigami.Units.largeSpacing

        // This is a bit of a hack. Fetch unreceived data periodically,
        // in case the device looses connection, or similar
        Timer {
            interval: 5000; // every 5s
            running: true;
            repeat: true
            onTriggered: conversationModel.requestMessages(ConversationModel.UiTimer)
        }

        header: Item {
            height: Kirigami.Units.largeSpacing * 2
        }
        headerPositioning: ListView.InlineHeader

        delegate: ChatMessage {
            required property var model
            required property int index

            name: model.sender
            messageBody: model.display
            sentByMe: model.fromMe
            dateTime: new Date(model.date)
            multiTarget: isMultitarget
            attachmentList: model.attachments

            width: viewport.width

            onMessageCopyRequested: {
                SmsHelper.copyToClipboard(message)
            }

            function prefetch() {
                // The distance of the currently visible item from the bottom of the list,
                // below which we start prefetching.
                // E.g if a user is looking at item 80 of a 100 element list,
                // we would start prefetching with the prefetchRange set at 20
                const prefetchRange = 20;
                if (conversationModel.rowCount() - index + 1 < prefetchRange) {
                    conversationModel.requestMessages(ConversationModel.UiPrefetch);
                }
            }

            ListView.onAdd: prefetch()
            ListView.onReused: prefetch()
        }
    }

    footer: SendingArea {
        id: sendingArea

        width: parent.width
        addresses: page.addresses
    }
}
