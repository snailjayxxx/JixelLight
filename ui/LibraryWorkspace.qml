import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Responsive, virtualized library grid with an async thumbnail provider.
// Selecting photos keeps the same PhotoController state as Develop mode.
Rectangle {
    id: root
    objectName: "libraryWorkspace"
    required property var controller
    signal editRequested()
    property int filterMode: 0 // all, picks, rejects, rating >= 3
    property string searchText: ""
    property string filterAlbum: ""
    property string filterLabel: "none"
    property int sortMode: 0 // catalog order, filename, rating descending
    property int selectionAnchor: -1
    property var visiblePhotos: {
        let rows = controller.library.filter(row =>
            (filterMode !== 1 || row.flag === "pick") &&
            (filterMode !== 2 || row.flag === "reject") &&
            (filterMode !== 3 || row.rating >= 3) &&
            (!filterAlbum || row.albums.indexOf(filterAlbum) >= 0) &&
            (filterLabel === "none" || row.label === filterLabel) &&
            (!searchText || (row.name + " " + row.keywords.join(" ")).toLowerCase().indexOf(searchText.toLowerCase()) >= 0))
        if (sortMode === 1) rows.sort((a,b) => a.name.localeCompare(b.name) || a.index-b.index)
        if (sortMode === 2) rows.sort((a,b) => b.rating-a.rating || a.index-b.index)
        return rows
    }
    function choose(index, modifiers) {
        let selection = controller.selectedIndices.slice()
        const order = visiblePhotos.map(row => row.index)
        const anchor = order.indexOf(selectionAnchor), position = order.indexOf(index)
        if ((modifiers & Qt.ShiftModifier) && anchor >= 0 && position >= 0)
            selection = order.slice(Math.min(anchor,position),Math.max(anchor,position)+1)
        else if (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
            const existing = selection.indexOf(index)
            if (existing >= 0) selection.splice(existing,1)
            else selection.push(index)
            selectionAnchor = index
        } else { selection = [index]; selectionAnchor = index }
        controller.selectPhoto(index)
        controller.setPhotoSelection(selection)
    }
    color: "#0d1014"
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.t("所有照片", "All Photos")
                font.pixelSize: 20
                font.bold: true
                color: "#e1e8f0"
            }
            Label {
                text: root.controller.library.length + root.t(" 张", " photos")
                color: "#8795a5"
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                Layout.preferredWidth: 155
                model: [root.t("全部", "All"), root.t("已选", "Picks"),
                        root.t("拒绝", "Rejects"), root.t("三星以上", "3+ Stars")]
                currentIndex: root.filterMode
                onActivated: root.filterMode = currentIndex
            }
            Button {
                text: root.t("导入照片", "Import Photos")
                onClicked: root.controller.openImportDialog()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField { Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: root.t("搜索文件名 / 关键词", "Search filename / keywords"); onTextChanged: root.searchText = text.trim() }
            ComboBox { objectName: "catalogAlbumFilter"; Layout.preferredWidth: 140; model: [root.t("全部相册", "All albums")].concat(Array.from(root.controller.albumNames)); currentIndex: root.filterAlbum ? model.indexOf(root.filterAlbum) : 0; onModelChanged: { if (root.filterAlbum && model.indexOf(root.filterAlbum) < 0) root.filterAlbum = "" } onActivated: root.filterAlbum = currentIndex === 0 ? "" : currentText }
            ComboBox { Layout.preferredWidth: 110; currentIndex: ["none","red","yellow","green","blue","purple"].indexOf(root.filterLabel); model: [root.t("全部颜色", "All labels"),root.t("红", "Red"),root.t("黄", "Yellow"),root.t("绿", "Green"),root.t("蓝", "Blue"),root.t("紫", "Purple")]; onActivated: root.filterLabel = ["none","red","yellow","green","blue","purple"][currentIndex] }
            ComboBox { Layout.preferredWidth: 130; currentIndex: root.sortMode; model: [root.t("目录顺序", "Catalog order"),root.t("文件名", "Filename"),root.t("评分 ↓", "Rating ↓")]; onActivated: root.sortMode = currentIndex }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#2c343e" }
        GridView {
            id: grid
            objectName: "libraryPhotoGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.visiblePhotos
            cellWidth: Math.max(180, width / Math.max(1, Math.floor(width / 225)))
            cellHeight: 224
            delegate: Item {
                required property int index
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 5
                    radius: 6
                    color: modelData.selected ? "#293d51" : "#1b2129"
                    border.width: modelData.index === root.controller.currentIndex ? 2 : 1
                    border.color: modelData.index === root.controller.currentIndex ? "#74aee9" : "#303944"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 7
                        spacing: 5
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#11151a"
                            radius: 3
                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                source: "image://thumbnails/" + encodeURIComponent(modelData.path)
                                sourceSize: Qt.size(384, 280)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                            }
                            Label {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 4
                                text: (modelData.virtual ? root.t("副本 ", "Copy ") : "") + modelData.type + (modelData.flag === "pick" ? "  P" : modelData.flag === "reject" ? "  X" : "")
                                color: modelData.raw ? "#a7f1d6" : "#d1dbe5"
                                font.bold: true
                                font.pixelSize: 9
                                padding: 3
                                background: Rectangle { color: "#b51c2530"; radius: 3 }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: (modelData.label !== "none" ? "● " : "") + modelData.name + (modelData.rating > 0 ? "  ★" + modelData.rating : "")
                            color: ({red:"#ff8585",yellow:"#efce76",green:"#8bd5a5",blue:"#8bb8ed",purple:"#c9a1e5"})[modelData.label] || "#dce5ee"
                            elide: Text.ElideMiddle
                            font.pixelSize: 11
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.keywords.join(", ")
                            visible: text.length > 0
                            color: "#8291a2"; font.pixelSize: 9; elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onClicked: function(mouse) { root.choose(modelData.index,mouse.modifiers) }
                        onDoubleClicked: {
                            root.controller.selectPhoto(modelData.index)
                            root.editRequested()
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Label { text: root.controller.selectedIndices.length + root.t(" 张已选 / ", " selected / ") + root.visiblePhotos.length + root.t(" 张显示", " shown"); color: "#a5b4c4" }
                Button { text: root.t("选择显示照片", "Select shown"); onClicked: root.controller.setPhotoSelection(root.visiblePhotos.map(row => row.index)) }
                Button { text: root.t("清空选择", "Clear selection"); onClicked: root.controller.setPhotoSelection([]) }
                Item { Layout.fillWidth: true }
                ComboBox { id: batchRating; model: ["0 ★","1 ★","2 ★","3 ★","4 ★","5 ★"] }
                Button { text: root.t("评分", "Rate"); enabled: root.controller.selectedIndices.length > 0; onClicked: root.controller.setSelectionRating(batchRating.currentIndex) }
                Button { text: "P"; enabled: root.controller.selectedIndices.length > 0; onClicked: root.controller.setSelectionFlag("pick") }
                Button { text: "X"; enabled: root.controller.selectedIndices.length > 0; onClicked: root.controller.setSelectionFlag("reject") }
                Button { text: "—"; enabled: root.controller.selectedIndices.length > 0; onClicked: root.controller.setSelectionFlag("none") }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: keywords; Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: root.t("替换所选关键词，英文逗号分隔", "Replace selected keywords, comma separated"); maximumLength: 5200 }
                Button { text: root.t("设置关键词", "Set keywords"); enabled: root.controller.selectedIndices.length > 0; onClicked: root.controller.setSelectionKeywords(keywords.text) }
                ComboBox { id: batchLabel; model: [root.t("无色", "None"),root.t("红", "Red"),root.t("黄", "Yellow"),root.t("绿", "Green"),root.t("蓝", "Blue"),root.t("紫", "Purple")] }
                Button { text: root.t("颜色标签", "Label"); enabled: root.controller.selectedIndices.length > 0; onClicked: root.controller.setSelectionLabel(["none","red","yellow","green","blue","purple"][batchLabel.currentIndex]) }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: albumName; Layout.fillWidth: true; Layout.minimumWidth: 0; maximumLength: 80; placeholderText: root.t("相册名称", "Album name") }
                Button { text: root.t("加入相册", "Add to album"); enabled: root.controller.selectedIndices.length > 0 && albumName.text.trim().length > 0; onClicked: root.controller.addSelectionToAlbum(albumName.text) }
                TextField { id: versionName; Layout.preferredWidth: 140; maximumLength: 80; placeholderText: root.t("版本名称", "Version name") }
                Button { text: root.t("虚拟副本", "Virtual copy"); enabled: root.controller.hasImage; onClicked: { if (root.controller.createVirtualCopy(versionName.text)) versionName.clear() } }
                Button { text: root.t("移出相册", "Remove from album"); enabled: root.controller.selectedIndices.length > 0 && albumName.text.trim().length > 0; onClicked: root.controller.removeSelectionFromAlbum(albumName.text) }
            }
            Label { Layout.fillWidth: true; text: root.t("Ctrl / ⌘ 多选，Shift 范围选择。集合相册不移动原片；关键词设置会替换所选照片的关键词。", "Ctrl / ⌘ toggles; Shift selects a range. Albums keep originals in place; keyword edits replace selected keywords."); color: "#8291a2"; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
        Label {
            visible: root.controller.library.length === 0
            Layout.alignment: Qt.AlignHCenter
            text: root.t("导入照片以建立图库，双击缩略图开始编辑。", "Import photos to start a library. Double-click a thumbnail to develop.")
            color: "#8b99a8"
        }
    }
}
