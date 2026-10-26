import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/echo/quick/theme"

ColumnLayout {
    id: browser
    required property var controller
    required property real windowWidth
    required property real fullContentWidth
    property real horizontalMargin: 30
    property bool queueOpen: false
    readonly property bool searchActive: searchBox.activeFocus
    signal cardContextRequested(Item sender, point position, int row)
    signal trackContextRequested(Item sender, point position, int row)
    signal queueDismissRequested()
    function focusSearch() {
        searchBox.forceActiveFocus();
        searchBox.selectAll();
    }
    function restoreScroll() {
        const view = controller.tracks.count > 0 ? tableArea.view
            : controller.page.kind === "artists" ? artistList : cardViewport.view;
        view.contentY = Math.max(0, Math.min(controller.scroll(), view.contentHeight - view.height));
        pageContent.switching = false;
        pageContent.opacity = 1;
    }
    Connections {
        target: browser.controller
        function onNavigationStarted() {
            pageContent.switching = true;
            pageContent.opacity = 0;
        }
        function onPageChanged() {
            if (browser.controller.page.kind !== "search" && searchBox.text.length > 0)
                searchBox.text = "";
            else if (browser.controller.page.kind === "search" && searchBox.text.trim() !== browser.controller.page.query)
                searchBox.text = browser.controller.page.query;
            if (!browser.controller.page.busy)
                Qt.callLater(browser.restoreScroll);
        }
    }
    objectName: "libraryContent"
    spacing: 16
    RowLayout {
        Layout.fillWidth: true
        ActionButton {
            objectName: "backButton"
            symbol: "back"
            filled: true
            implicitWidth: 32
            implicitHeight: 32
            iconSize: 18
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            enabled: browser.controller.page.canBack
            label: "返回"
            onClicked: browser.controller.back()
        }
        TextField {
            id: searchBox
            objectName: "searchField"
            Layout.preferredWidth: Math.min(330, browser.windowWidth * 0.3)
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            placeholderText: "搜索歌曲、专辑、艺术家"
            color: Theme.cf3edf9
            placeholderTextColor: Theme.c81798e
            font.pixelSize: 12
            leftPadding: 14
            rightPadding: 32
            background: Rectangle {
                color: Theme.c26222f
                radius: 9
                border.color: searchBox.activeFocus ? Theme.c826499 : Theme.c10ffffff
            }
            onTextEdited: browser.controller.search(text)
            Keys.onEscapePressed: {
                text = "";
                browser.controller.search("");
                browser.forceActiveFocus();
            }
            ActionButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 30
                implicitHeight: 30
                symbol: "collapse"
                label: "清除搜索"
                visible: searchBox.text.length > 0
                rotation: -90
                onClicked: {
                    searchBox.text = "";
                    browser.controller.search("");
                }
            }
        }
        Item {
            Layout.fillWidth: true
        }
    }
    ColumnLayout {
        id: pageContent
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 16
        objectName: "pageContent"
        property bool switching: false
        enabled: !switching
        opacity: 1
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion(browser.controller.reduceMotion, pageContent.switching ? Theme.motionShort : 170)
                easing.type: Easing.OutCubic
            }
        }
        LibraryHeader { controller: browser.controller }
        ArtistListView {
            id: artistList
            controller: browser.controller
            onContextRequested: (sender, position, row) => browser.cardContextRequested(sender, position, row)
        }
        AlbumCardsView {
            id: cardViewport
            controller: browser.controller
            fullContentWidth: browser.fullContentWidth
            queueOpen: browser.queueOpen
            onContextRequested: (sender, position, row) => browser.cardContextRequested(sender, position, row)
            onQueueDismissRequested: browser.queueDismissRequested()
        }
        TrackTable {
            id: tableArea
            controller: browser.controller
            queueOpen: browser.queueOpen
            onContextRequested: (sender, position, row) => browser.trackContextRequested(sender, position, row)
            onQueueDismissRequested: browser.queueDismissRequested()
        }
        LibraryEmptyState { controller: browser.controller }
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: browser.controller.page.busy
            visible: running
            implicitWidth: 30
            implicitHeight: 30
        }
    }
}
