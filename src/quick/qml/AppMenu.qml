import QtQuick
import QtQuick.Controls
Menu {
    id: menu
    property var entries: []
    property var owned: []
    signal chosen(int token)
    width: 230
    padding: 6
    font.pixelSize: 13
    background: Rectangle { color: "#292330"; radius: 10; antialiasing: true; border.color: "#51465f" }
    delegate: MenuItem {
        id: submenuItem
        implicitHeight: 34
        contentItem: Text { text: submenuItem.text; color: "#eee3ff"; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 6; color: submenuItem.highlighted ? "#51415f" : "transparent" }
        arrow: Text { x: parent.width - width - 10; y: (parent.height-height)/2; text: "›"; color: "#aaa1b7" }
    }
    Component {
        id: leaf
        MenuItem {
            id: entry
            property int token: 0
            implicitHeight: 34
            contentItem: Text { text: entry.text; color: "#eee3ff"; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { radius: 6; color: entry.highlighted ? "#51415f" : "transparent" }
            onTriggered: menu.chosen(token)
        }
    }
    Component {
        id: separator
        MenuSeparator {
            implicitHeight: 9
            contentItem: Rectangle { implicitWidth: 200; implicitHeight: 1; color: "#20ffffff" }
        }
    }
    function rebuild() {
        for (const object of owned) {
            if (object.isSubmenu) removeMenu(object); else removeItem(object);
            object.destroy();
        }
        const next = [];
        for (const entry of entries) {
            let object;
            if (entry.children) {
                const component = Qt.createComponent("AppMenu.qml");
                object = component.createObject(menu.parent, {title:entry.text, entries:entry.children});
                object.chosen.connect(function(token) { menu.close(); menu.chosen(token); });
                addMenu(object);
            } else {
                object = entry.separator ? separator.createObject(menu.contentItem) : leaf.createObject(menu.contentItem, {text:entry.text, token:entry.token});
                addItem(object);
            }
            next.push(object);
        }
        owned = next;
    }
    readonly property bool isSubmenu: true
    onEntriesChanged: rebuild()
}
