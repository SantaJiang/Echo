import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Files

Item {
    id: dialogs
    property var request: ({})
    property int token: 0
    property bool syncing: false
    readonly property bool active: token > 0 || settings.visible
    function answer(values) {
        if (!syncing && token > 0) {
            const current = token;
            token = 0;
            client.answerDialog(current, values);
        }
    }
    function sync() {
        syncing = true;
        nameDialog.close(); messageDialog.close(); trackDialog.close();
        filePicker.close(); folderPicker.close(); artPicker.close();
        request = client.dialog;
        token = Number(request.token || 0);
        if (token > 0) {
            settings.close();
            if (request.kind === "name") {
                nameField.text = request.initial || "";
                nameDialog.open();
                Qt.callLater(function() { nameField.forceActiveFocus(); nameField.selectAll(); });
            } else if (request.kind === "file") {
                if (request.mode === "folder") folderPicker.open();
                else filePicker.open();
            } else if (request.kind === "track") {
                trackTitle.text = request.trackTitle || "";
                artist.text = request.artist || "";
                album.text = request.album || "";
                albumArtist.text = request.albumArtist || "";
                trackNumber.text = String(request.trackNumber || 0);
                year.text = String(request.year || 0);
                trackDialog.artFile = "";
                trackDialog.open();
            } else messageDialog.open();
        }
        syncing = false;
    }
    Connections {
        target: client
        function onDialogChanged() { dialogs.sync(); }
        function onSettingsRequested() { if (!dialogs.token) settings.open(); }
    }
    AppDialog {
        id: nameDialog
        objectName: "nameDialog"
        title: dialogs.request.title || ""
        onRejected: dialogs.answer({accepted:false})
        contentItem: ColumnLayout {
            spacing: 20
            FormField {
                id: nameField
                objectName: "nameField"
                Layout.fillWidth: true
                Accessible.name: "名称"
                maximumLength: 200
                onAccepted: if (text.trim().length) dialogs.answer({accepted:true, name:text})
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10
                PillButton { text: "取消"; onClicked: dialogs.answer({accepted:false}) }
                PillButton { text: dialogs.request.accept || "完成"; primary: true; enabled: nameField.text.trim().length > 0; onClicked: dialogs.answer({accepted:true, name:nameField.text}) }
            }
        }
    }
    AppDialog {
        id: messageDialog
        objectName: "messageDialog"
        title: dialogs.request.title || ""
        onRejected: dialogs.answer({accepted:false})
        contentItem: ColumnLayout {
            spacing: 18
            Label {
                Layout.fillWidth: true
                text: dialogs.request.text || ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: "#ddd4e8"
                font.pixelSize: 13
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, detailText.implicitHeight)
                visible: (dialogs.request.details || "").length > 0
                clip: true
                TextArea {
                    id: detailText
                    text: dialogs.request.details || ""
                    textFormat: TextEdit.PlainText
                    readOnly: true; selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: "#aaa1b7"
                    font.pixelSize: 11
                    background: Rectangle { color: "#17151d"; radius: 8 }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10
                PillButton {
                    text: "取消"
                    visible: dialogs.request.kind !== "message"
                    onClicked: dialogs.answer({accepted:false})
                }
                PillButton {
                    visible: dialogs.request.kind === "unavailable"
                    text: "重新检查"
                    onClicked: dialogs.answer({choice:"check"})
                }
                PillButton {
                    primary: true
                    text: dialogs.request.kind === "unavailable" ? "重新定位…" : (dialogs.request.accept || "完成")
                    onClicked: dialogs.answer(dialogs.request.kind === "unavailable" ? {choice:"locate"} : {accepted:true})
                }
            }
        }
    }
    Files.FileDialog {
        id: filePicker
        objectName: "filePicker"
        title: dialogs.request.title || ""
        options: Files.FileDialog.DontUseNativeDialog
        fileMode: dialogs.request.mode === "save" ? Files.FileDialog.SaveFile : Files.FileDialog.OpenFile
        nameFilters: dialogs.request.filters && dialogs.request.filters.length ? dialogs.request.filters : ["所有文件 (*)"]
        onAccepted: dialogs.answer({accepted:true, path:selectedFile.toString()})
        onRejected: dialogs.answer({accepted:false})
    }
    Files.FolderDialog {
        id: folderPicker
        objectName: "folderPicker"
        title: dialogs.request.title || ""
        options: Files.FolderDialog.DontUseNativeDialog
        onAccepted: dialogs.answer({accepted:true, path:selectedFolder.toString()})
        onRejected: dialogs.answer({accepted:false})
    }
    Files.FileDialog {
        id: artPicker
        title: "选择封面"
        options: Files.FileDialog.DontUseNativeDialog
        nameFilters: ["封面图片 (*.jpg *.jpeg *.png)"]
        onAccepted: trackDialog.artFile = selectedFile.toString()
    }
    AppDialog {
        id: trackDialog
        objectName: "trackDialog"
        property string artFile: ""
        title: "歌曲信息"
        onRejected: dialogs.answer({accepted:false})
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                spacing: 16
                Cover { Layout.preferredWidth: 100; Layout.preferredHeight: 100; radius: 12; artwork: trackDialog.artFile || dialogs.request.artwork || "" }
                ColumnLayout {
                    Layout.fillWidth: true
                    PillButton { text: "更换封面…"; enabled: !dialogs.request.cue; onClicked: artPicker.open() }
                    Label { Layout.fillWidth: true; text: dialogs.request.cue ? "CUE 曲目的标题和艺术家会写回 CUE 文件" : "修改会写入音乐文件"; color: "#aaa1b7"; font.pixelSize: 11; wrapMode: Text.Wrap }
                }
            }
            GridLayout {
                columns: 2; columnSpacing: 14; rowSpacing: 10
                Layout.fillWidth: true
                Label { text: "标题"; color: "#aaa1b7" }
                FormField { id: trackTitle; objectName: "trackTitleField"; Layout.fillWidth: true; Accessible.name: "标题" }
                Label { text: "艺术家"; color: "#aaa1b7" }
                FormField { id: artist; Layout.fillWidth: true; Accessible.name: "艺术家" }
                Label { text: "专辑"; color: "#aaa1b7" }
                FormField { id: album; Layout.fillWidth: true; enabled: !dialogs.request.cue; Accessible.name: "专辑" }
                Label { text: "专辑艺术家"; color: "#aaa1b7" }
                FormField { id: albumArtist; Layout.fillWidth: true; enabled: !dialogs.request.cue; Accessible.name: "专辑艺术家" }
                Label { text: "曲目编号"; color: "#aaa1b7" }
                FormField { id: trackNumber; Layout.fillWidth: true; validator: IntValidator { bottom: 0; top: 999 } Accessible.name: "曲目编号" }
                Label { text: "年份"; color: "#aaa1b7" }
                FormField { id: year; Layout.fillWidth: true; validator: IntValidator { bottom: 0; top: 9999 } Accessible.name: "年份" }
            }
            Label { Layout.fillWidth: true; text: dialogs.request.path || ""; textFormat: Text.PlainText; color: "#847e90"; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 10
                PillButton { text: "取消"; onClicked: dialogs.answer({accepted:false}) }
                PillButton {
                    text: "保存"; primary: true
                    enabled: trackNumber.acceptableInput && year.acceptableInput
                    onClicked: dialogs.answer({accepted:true, title:trackTitle.text, artist:artist.text, album:album.text,
                        albumArtist:albumArtist.text, trackNumber:Number(trackNumber.text), year:Number(year.text), artFile:trackDialog.artFile})
                }
            }
        }
    }
    AppDialog {
        id: settings
        objectName: "settingsDialog"
        title: "设置"
        contentItem: ColumnLayout {
            spacing: 12
            AppCheck { Layout.fillWidth: true; text: "从页面播放时替换当前队列"; preference: "replaceQueue" }
            AppCheck { Layout.fillWidth: true; text: "响度均衡"; preference: "normalize" }
            Label { Layout.fillWidth: true; text: "让不同歌曲的音量更接近，分析结果会自动缓存。"; color: "#aaa1b7"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.leftMargin: 36 }
            AppCheck { Layout.fillWidth: true; text: "使用 ReplayGain 标签"; preference: "replayGain" }
            AppCheck { Layout.fillWidth: true; text: "独占音频设备"; preference: "exclusive" }
            AppCheck { Layout.fillWidth: true; text: "空间音频"; preference: "spatial"; visible: Qt.platform.os === "osx" }
            Label { Layout.fillWidth: true; text: client.preferences.spatialStatus || ""; visible: Qt.platform.os === "osx" && text.length > 0; color: "#aaa1b7"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.leftMargin: 36 }
            AppCheck { Layout.fillWidth: true; text: "减少动态效果"; preference: "reduceMotion" }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#20ffffff"; Layout.topMargin: 8; Layout.bottomMargin: 8 }
            RowLayout {
                Layout.fillWidth: true
                PillButton { text: "迷你播放器"; onClicked: { settings.close(); client.showMini(); } }
                Item { Layout.fillWidth: true }
                PillButton { text: "完成"; primary: true; onClicked: settings.close() }
            }
        }
    }
}
