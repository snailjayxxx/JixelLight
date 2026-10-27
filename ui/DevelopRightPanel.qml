import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

// Lightroom-inspired collapsible editing panels. All commands still route
// through the existing JixelLight controller and CPU/GPU processing chain.
Rectangle {
    id: root
    required property var controller
    property var brushEditor: null
    property bool brushExpanded: false
    readonly property var brushOptions: {
        let options=[{id:"",label:root.t("新图层（绘制后创建）","New layer (paint to create)")}]
        const layers=root.controller.brushLayers
        for (let i=0;i<layers.length;++i) options.push({id:layers[i].id,label:root.t("笔刷 ","Brush ")+(i+1)+" · "+layers[i].strokes})
        return options
    }
    readonly property var currentBrush: {
        if (!brushEditor) return null
        const layers=controller.brushLayers
        for (const layer of layers) if (layer.id === brushEditor.layerId) return layer
        return null
    }
    property bool brushAutoPositionOwned: false
    property int brushRevealAttempts: 0
    property int brushRevealStableFrames: 0
    readonly property bool brushRevealPending: revealBrush.running
    readonly property Flickable brushToolFlick: toolScroll.contentItem as Flickable
    function cancelBrushReveal() {
        brushAutoPositionOwned=false; revealBrush.stop()
    }
    function requestBrushReveal() {
        brushAutoPositionOwned=true
        brushRevealAttempts=0; brushRevealStableFrames=0
        revealBrush.restart()
    }
    onVisibleChanged: if (!visible) cancelBrushReveal()
    onBrushExpandedChanged: if (!brushExpanded) cancelBrushReveal()
    Connections {
        target: root.brushEditor
        function onActiveChanged() {
            if (root.brushEditor.active) { root.brushExpanded=true; root.requestBrushReveal() }
            else root.cancelBrushReveal()
        }
    }
    Connections {
        target: root.controller
        function onLanguageChanged() {
            if (root.brushAutoPositionOwned && root.visible && root.brushExpanded && root.brushEditor && root.brushEditor.active)
                root.requestBrushReveal()
        }
    }
    Connections {
        target: root.brushToolFlick
        ignoreUnknownSignals: true
        function onDraggingChanged() { if (root.brushToolFlick && root.brushToolFlick.dragging) root.cancelBrushReveal() }
        function onFlickingChanged() { if (root.brushToolFlick && root.brushToolFlick.flicking) root.cancelBrushReveal() }
    }
    Timer {
        id: revealBrush; interval: 16; repeat: true
        onTriggered: {
            if (++root.brushRevealAttempts>60 || !root.visible || !root.brushExpanded || !root.brushEditor || !root.brushEditor.active) {
                root.cancelBrushReveal(); return
            }
            const flick=root.brushToolFlick
            if (!flick || !flick.contentItem || flick.height<=0) return
            const headerContentY=brushPanelHeader.mapToItem(flick.contentItem,0,0).y
            const exposureContentY=brushExposureControl.mapToItem(flick.contentItem,0,0).y
            if (flick.contentHeight<=flick.height || brushPanelHeader.height<=0 || exposureContentY<headerContentY+brushPanelHeader.height) return
            const top=brushPanelHeader.mapToItem(flick,0,0).y
            const bottom=brushExposureControl.mapToItem(flick,0,brushExposureControl.height).y
            if (top>=-1 && bottom<=flick.height+1 && brushExposureControl.height>0) {
                if (++root.brushRevealStableFrames>=2) stop()
                return
            }
            root.brushRevealStableFrames=0
            const y=brushPanelHeader.mapToItem(flick.contentItem,0,0).y
            flick.contentY=Math.max(0,Math.min(y,flick.contentHeight-flick.height))
        }
    }
    property bool sonyExpanded: false
    property bool basicExpanded: true
    property bool colorExpanded: true
    property bool mixerExpanded: false
    property bool curveExpanded: false
    property bool exifExpanded: false
    property bool correctionsExpanded: false
    property bool vignetteExpanded: false
    property bool linearMaskExpanded: false
    property bool radialMaskExpanded: false
    property bool blackWhiteExpanded: false
    property bool colorGradingExpanded: false
    color: "#15191f"
    border.color: "#292f37"

    Settings {
        category: "DevelopPanelSections"
        property alias sonyExpanded: root.sonyExpanded
        property alias basicExpanded: root.basicExpanded
        property alias colorExpanded: root.colorExpanded
        property alias mixerExpanded: root.mixerExpanded
        property alias curveExpanded: root.curveExpanded
        property alias exifExpanded: root.exifExpanded
        property alias correctionsExpanded: root.correctionsExpanded
        property alias vignetteExpanded: root.vignetteExpanded
        property alias linearMaskExpanded: root.linearMaskExpanded
        property alias radialMaskExpanded: root.radialMaskExpanded
        property alias blackWhiteExpanded: root.blackWhiteExpanded
        property alias colorGradingExpanded: root.colorGradingExpanded
    }
    function t(zh, en) { return controller.language === "zh_CN" ? zh : en }
    function meta(key) {
        const value = controller.currentMetadata[key]
        return value === undefined || value === null || value === "" ? "—" : value
    }
    function cameraName() {
        const make = meta("make")
        const model = meta("model")
        if (make === "—") return model
        if (model === "—") return make
        return make + " " + model
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // Sticky Scopes: the existing 1024-bin GPU histogram is kept pinned.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 8
            Layout.bottomMargin: 8
                Label { text: root.t("当前编辑图像 · 专业示波器", "CURRENT EDIT · SCOPES"); color: "#8e9aa8"; font.bold: true; font.pixelSize: 11; Layout.topMargin: 10 }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        objectName: "scopeModeChoice"; Layout.fillWidth: true
                        model: [root.t("直方图", "Histogram"),root.t("亮度波形", "Luma waveform"),"RGB Parade",root.t("矢量示波器", "Vectorscope")]
                        currentIndex: ["histogram","waveform","parade","vectorscope"].indexOf(root.controller.scopeMode)
                        onActivated: root.controller.scopeMode=["histogram","waveform","parade","vectorscope"][currentIndex]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; visible: root.controller.scopeMode==="histogram"
                    Button { id: rgbButton; text: "RGB"; checkable: true; checked: true; onClicked: { checked = true; lumaButton.checked = false } }
                    Button { id: lumaButton; text: root.t("亮度", "Luma"); checkable: true; onClicked: { checked = true; rgbButton.checked = false } }
                    Item { Layout.fillWidth: true }
                    Label { text: "1024 bins · " + root.controller.scopesPixelCount; color: "#738293"; font.pixelSize: 10 }
                }
                RowLayout {
                    Label { text: root.controller.scopeMode==="histogram" ? root.controller.scopesStatus : root.controller.scopePlotStatus; wrapMode: Text.WordWrap; color: "#8e9aa8"; font.pixelSize: 10; Layout.fillWidth: true }
                    CheckBox { objectName: "exactScopesCheck"; text: root.t("全分辨率", "Full resolution"); checked: root.controller.exactScopes; onToggled: root.controller.exactScopes=checked }
                }
                HistogramView {
                    visible: root.controller.scopeMode==="histogram"
                    Layout.fillWidth: true; Layout.preferredHeight: 115
                    redData: root.controller.redHistogram; greenData: root.controller.greenHistogram; blueData: root.controller.blueHistogram
                    lumaData: root.controller.lumaHistogram; showLuma: lumaButton.checked
                }
                ScopePlotView {
                    objectName: "scopePlotView"
                    visible: root.controller.scopeMode!=="histogram"
                    Layout.fillWidth: true; Layout.preferredHeight: 170
                    mode: root.controller.scopeMode; imageUrl: root.controller.scopePlotUrl; current: root.controller.scopePlotCurrent
                }
                Label {
                    visible: root.controller.scopeMode!=="histogram"; Layout.fillWidth: true
                    text: root.controller.scopeMode==="vectorscope" ? root.t("sRGB 编码 · Cb/Cr · 100% 色标", "Encoded sRGB · Cb/Cr · 100% targets") : root.t("sRGB 编码 · 1024 级 · 水平位置", "Encoded sRGB · 1024 levels · horizontal position")
                    color: "#738293"; font.pixelSize: 10
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: root.t("阴影裁切  ", "Shadows clipped  ") + root.controller.shadowClipPercent.toFixed(3) + "%"; color: root.controller.shadowClipPercent > 0.1 ? "#ffb066" : "#8b98a6"; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Label { text: root.t("高光裁切  ", "Highlights  ") + root.controller.highlightClipPercent.toFixed(3) + "%"; color: root.controller.highlightClipPercent > 0.1 ? "#ff8e8e" : "#8b98a6"; font.pixelSize: 10 }
                }

        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 2
            Label { text: root.t("评分", "Rating"); color: "#a2b1c0"; font.pixelSize: 10 }
            Repeater {
                model: 5
                delegate: ToolButton {
                    required property int index
                    text: index < root.controller.currentRating ? "★" : "☆"
                    enabled: root.controller.hasImage
                    font.pixelSize: 15
                    onClicked: root.controller.setRating(index + 1)
                }
            }
            ToolButton {
                text: "P"
                enabled: root.controller.hasImage
                highlighted: root.controller.currentFlag === "pick"
                onClicked: root.controller.setFlag(root.controller.currentFlag === "pick" ? "none" : "pick")
            }
            ToolButton {
                text: "X"
                enabled: root.controller.hasImage
                highlighted: root.controller.currentFlag === "reject"
                onClicked: root.controller.setFlag(root.controller.currentFlag === "reject" ? "none" : "reject")
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "0"
                enabled: root.controller.hasImage
                onClicked: root.controller.setRating(0)
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#303840" }
        ScrollView {
            id: toolScroll; objectName: "developToolScroll"
            WheelHandler {
                target: null; blocking: false
                onWheel: function(event) { root.cancelBrushReveal(); event.accepted=false }
            }
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                x: 10
                width: Math.max(280, toolScroll.availableWidth - 20)
                spacing: 6

                Label { text: root.t("裁切 / 拉直 / 旋转（CPU）", "CROP / STRAIGHTEN / ROTATE (CPU)"); color: "#a3b2c1"; font.bold: true }
                Button {
                    objectName: "beginInteractiveCrop"; Layout.fillWidth: true
                    text: root.controller.cropEditing ? root.t("取消裁剪编辑", "Cancel crop editing") : root.t("在画布上裁剪…", "Crop on canvas…")
                    enabled: root.controller.previewReady
                    onClicked: root.controller.cropEditing ? root.controller.cancelCrop() : root.controller.beginCrop()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "↶ 90°"; enabled: root.controller.hasImage; onClicked: root.controller.rotatePhoto(-1) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "↷ 90°"; enabled: root.controller.hasImage; onClicked: root.controller.rotatePhoto(1) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("水平翻转", "Flip H"); enabled: root.controller.hasImage; onClicked: root.controller.flipPhoto(true) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("垂直翻转", "Flip V"); enabled: root.controller.hasImage; onClicked: root.controller.flipPhoto(false) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: root.t("居中裁切", "Center crop"); color: "#a3b2c1" }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "1:1"; enabled: root.controller.previewReady; onClicked: root.controller.setCropAspect(1) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "3:2"; enabled: root.controller.previewReady; onClicked: root.controller.setCropAspect(1.5) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "4:3"; enabled: root.controller.previewReady; onClicked: root.controller.setCropAspect(4/3) }
                    Button { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.t("重置", "Reset"); enabled: root.controller.hasImage; onClicked: root.controller.resetGeometry() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: root.controller.previewReady && !root.controller.cropEditing
                    Label { text: root.t("拉直", "Straighten"); color: "#a3b2c1"; Layout.preferredWidth: 70 }
                    Slider {
                        objectName: "straightenAngle"; Layout.fillWidth: true
                        from: -45; to: 45; stepSize: .1; value: Number(root.controller.geometry.straighten || 0)
                        onMoved: root.controller.setStraighten(Math.round(value*10)/10)
                        onPressedChanged: if (!pressed) root.controller.finishInteraction()
                    }
                    Label { text: Number(root.controller.geometry.straighten || 0).toFixed(1)+"°"; color: "#aeb9c7"; Layout.preferredWidth: 42 }
                    Button { text: "0°"; Layout.preferredWidth: 42; onClicked: { root.controller.finishInteraction(); root.controller.setStraighten(0); root.controller.finishInteraction() } }
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#738293"; font.pixelSize: 10
                    text: root.t("拉直自动收边；随后在校正画面上裁切。", "Straighten trims edges; crop the corrected image.")
                }
                ToolButton {
                    objectName: "geometryCorrectionsToggle"; Layout.fillWidth: true
                    text: (root.correctionsExpanded ? "▾ " : "▸ ") + root.t("透视 / 手动镜头校正（CPU）", "PERSPECTIVE / MANUAL LENS (CPU)")
                    onClicked: root.correctionsExpanded = !root.correctionsExpanded
                    font.bold: true
                }
                ColumnLayout {
                    visible: root.correctionsExpanded; Layout.fillWidth: true; spacing: 3
                    enabled: root.controller.previewReady && !root.controller.cropEditing
                    Repeater {
                        model: [
                            { key: "perspectiveHorizontal", zh: "横向透视", en: "Horizontal", limit: .4, scale: 100, decimals: 1 },
                            { key: "perspectiveVertical", zh: "纵向透视", en: "Vertical", limit: .4, scale: 100, decimals: 1 },
                            { key: "distortion", zh: "畸变", en: "Distortion", limit: .3, scale: 100, decimals: 1 },
                            { key: "redCa", zh: "红色色差", en: "Red CA", limit: 2, scale: 1, decimals: 2 },
                            { key: "blueCa", zh: "蓝色色差", en: "Blue CA", limit: 2, scale: 1, decimals: 2 }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { text: root.t(modelData.zh, modelData.en); color: "#a3b2c1"; Layout.preferredWidth: 66; font.pixelSize: 11 }
                            Slider {
                                objectName: "geometry_"+modelData.key; Layout.fillWidth: true
                                from: -modelData.limit; to: modelData.limit; stepSize: modelData.scale===100 ? .001 : .01
                                value: Number(root.controller.geometry[modelData.key] || 0)
                                onMoved: root.controller.setGeometryAdjustment(modelData.key, Number(value.toFixed(modelData.scale===100 ? 3 : 2)))
                                onPressedChanged: if (!pressed) root.controller.finishInteraction()
                            }
                            Label { text: (Number(root.controller.geometry[modelData.key] || 0)*modelData.scale).toFixed(modelData.decimals)+"%"; color: "#aeb9c7"; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignRight }
                        }
                    }
                    Button {
                        objectName: "resetGeometryCorrections"; Layout.fillWidth: true
                        text: root.t("重置透视与镜头校正", "Reset perspective / lens")
                        onClicked: { root.controller.finishInteraction(); root.controller.resetGeometryCorrections(); root.controller.finishInteraction() }
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#738293"; font.pixelSize: 10
                        text: root.t("手动校正，自动收边。红／蓝色差调整相对绿通道的径向倍率；不使用镜头配置文件。", "Manual correction with automatic edge trim. Red / blue CA sets radial magnification relative to green; no lens profile is applied.")
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                ToolButton {
                    Layout.fillWidth: true
                    text: (root.sonyExpanded ? "▾ " : "▸ ") + root.t("相机 / Sony 创意外观", "CAMERA / SONY CREATIVE LOOK")
                    onClicked: root.sonyExpanded = !root.sonyExpanded
                    font.bold: true
                }
                SonyLookPanel {
                    controller: root.controller
                    Layout.fillWidth: true
                    visible: root.sonyExpanded
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        text: (root.basicExpanded ? "▾ " : "▸ ") + root.t("基础调整", "LIGHT")
                        onClicked: root.basicExpanded = !root.basicExpanded
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Button { text: root.t("全部重置", "Reset All"); enabled: root.controller.hasImage; onClicked: root.controller.resetAdjustments() }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.basicExpanded
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("曝光", "Exposure"); from: -5; to: 5; decimals: 2; value: root.controller.exposure; onEdited: root.controller.exposure = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("色温", "Temperature"); value: root.controller.temperature; onEdited: root.controller.temperature = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("色调", "Tint"); value: root.controller.tint; onEdited: root.controller.tint = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("对比度", "Contrast"); value: root.controller.contrast; onEdited: root.controller.contrast = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("高光", "Highlights"); value: root.controller.highlights; onEdited: root.controller.highlights = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("阴影", "Shadows"); value: root.controller.shadows; onEdited: root.controller.shadows = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("白色色阶", "Whites"); value: root.controller.whites; onEdited: root.controller.whites = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("黑色色阶", "Blacks"); value: root.controller.blacks; onEdited: root.controller.blacks = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("高光恢复", "Highlight Recovery"); from: 0; to: 100; value: root.controller.highlightRecovery; onEdited: root.controller.highlightRecovery = newValue }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                ToolButton {
                    Layout.fillWidth: true
                    text: (root.colorExpanded ? "▾ " : "▸ ") + root.t("颜色 / RAW 工作空间", "COLOR")
                    onClicked: root.colorExpanded = !root.colorExpanded
                    font.bold: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.colorExpanded
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("色相", "Hue"); from: -180; to: 180; value: root.controller.hue; onEdited: root.controller.hue = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("饱和度", "Saturation"); value: root.controller.saturation; onEdited: root.controller.saturation = newValue }
                AdjustmentSlider { Layout.fillWidth: true; label: root.t("自然饱和度", "Vibrance"); value: root.controller.vibrance; onEdited: root.controller.vibrance = newValue }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        objectName: "blackWhitePanelToggle"; Layout.fillWidth: true; font.bold: true
                        text: (root.blackWhiteExpanded ? "▾ " : "▸ ") + root.t("黑白 / 颜色混合", "BLACK & WHITE MIX")
                        onClicked: root.blackWhiteExpanded = !root.blackWhiteExpanded
                    }
                    Button {
                        objectName: "blackWhiteReset"; text: root.t("重置", "Reset")
                        enabled: root.controller.hasImage; onClicked: root.controller.resetBlackWhite()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 6; visible: root.blackWhiteExpanded
                    CheckBox {
                        objectName: "blackWhiteEnable"
                        text: root.t("启用黑白模式", "Enable monochrome")
                        checked: root.controller.blackWhite
                        enabled: root.controller.hasImage
                        onToggled: root.controller.blackWhite = checked
                    }
                    Repeater {
                        model: 8
                        delegate: AdjustmentSlider {
                            required property int index
                            Layout.fillWidth: true
                            enabled: root.controller.hasImage && root.controller.blackWhite
                            label: [root.t("红色", "Red"),root.t("橙色", "Orange"),root.t("黄色", "Yellow"),
                                    root.t("绿色", "Green"),root.t("青色", "Aqua"),root.t("蓝色", "Blue"),
                                    root.t("紫色", "Purple"),root.t("洋红", "Magenta")][index]
                            from: -100; to: 100
                            value: root.controller.blackWhiteMix[index]
                            onEdited: root.controller.setBlackWhiteMix(index,newValue)
                        }
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: "#738293"
                        text: root.t("每个颜色通道调整黑白亮度（±1 EV）；保留 Sony 外观，后续 RGB 曲线或 LUT 可能着色。",
                                     "Eight hue-weighted luminance bands (±1 EV); Sony Look is preserved. Later RGB curves or LUTs may tint the result.")
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        objectName: "colorGradingToggle"; Layout.fillWidth: true; font.bold: true
                        text: (root.colorGradingExpanded ? "▾ " : "▸ ") + root.t("色彩分级 / 三色轮", "COLOR GRADING / THREE WHEELS")
                        onClicked: root.colorGradingExpanded = !root.colorGradingExpanded
                    }
                    Button {
                        objectName: "colorGradingReset"; text: root.t("重置", "Reset")
                        enabled: root.controller.hasImage; onClicked: root.controller.resetColorGrading()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 5; visible: root.colorGradingExpanded
                    CheckBox {
                        objectName: "colorGradingEnable"
                        text: root.t("启用色彩分级", "Enable color grading")
                        checked: root.controller.gradingEnabled
                        enabled: root.controller.hasImage
                        onToggled: root.controller.gradingEnabled = checked
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 3
                        Repeater {
                            model: 3
                            delegate: ColumnLayout {
                                id: gradeWheelColumn
                                required property int index
                                readonly property real hue: Number(root.controller.gradingHue[index] || 0)
                                readonly property real saturation: Number(root.controller.gradingSaturation[index] || 0)
                                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    font.pixelSize: 10; color: "#b8c6d7"
                                    text: [root.t("阴影","Shadows"),root.t("中间调","Midtones"),
                                           root.t("高光","Highlights")][gradeWheelColumn.index]
                                }
                                Item {
                                    id: gradeWheelArea
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: 76; Layout.preferredHeight: 76
                                    opacity: root.controller.gradingEnabled ? 1 : .5
                                    Canvas {
                                        id: gradeWheelCanvas
                                        anchors.fill: parent
                                        onPaint: {
                                            const c = getContext("2d")
                                            const radius = width / 2 - 4
                                            const cx = width / 2, cy = height / 2
                                            c.clearRect(0,0,width,height)
                                            for (let deg=0; deg<360; deg+=4) {
                                                c.beginPath()
                                                c.moveTo(cx,cy)
                                                c.arc(cx,cy,radius,(deg-2.1)*Math.PI/180,(deg+2.1)*Math.PI/180)
                                                c.closePath()
                                                c.fillStyle = "hsl("+deg+",100%,55%)"
                                                c.fill()
                                            }
                                            const fade=c.createRadialGradient(cx,cy,0,cx,cy,radius)
                                            fade.addColorStop(0,"rgba(255,255,255,1)")
                                            fade.addColorStop(1,"rgba(255,255,255,0)")
                                            c.beginPath(); c.arc(cx,cy,radius,0,Math.PI*2)
                                            c.fillStyle=fade; c.fill()
                                            c.beginPath(); c.arc(cx,cy,radius,0,Math.PI*2)
                                            c.lineWidth=1; c.strokeStyle="#c4d2e1"; c.stroke()
                                        }
                                    }
                                    Rectangle {
                                        width: 10; height: 10; radius: 5
                                        color: Qt.hsla(gradeWheelColumn.hue/360,
                                                       gradeWheelColumn.saturation/100,.55,1)
                                        border.color: "#17202a"; border.width: 2
                                        x: gradeWheelArea.width/2 + Math.cos(gradeWheelColumn.hue*Math.PI/180)
                                           * (gradeWheelArea.width/2 - 4) * gradeWheelColumn.saturation/100 - width/2
                                        y: gradeWheelArea.height/2 + Math.sin(gradeWheelColumn.hue*Math.PI/180)
                                           * (gradeWheelArea.height/2 - 4) * gradeWheelColumn.saturation/100 - height/2
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: root.controller.hasImage && root.controller.gradingEnabled
                                        function updateWheel(px,py) {
                                            const dx=px-gradeWheelArea.width/2, dy=py-gradeWheelArea.height/2
                                            const hue=Math.round((Math.atan2(dy,dx)*180/Math.PI+360)%360)
                                            const sat=Math.round(100*Math.min(1,Math.sqrt(dx*dx+dy*dy)/(gradeWheelArea.width/2-4)))
                                            root.controller.setGradingWheel(gradeWheelColumn.index,hue,sat)
                                        }
                                        onPressed: function(mouse) { updateWheel(mouse.x,mouse.y) }
                                        onPositionChanged: function(mouse) { if (pressed) updateWheel(mouse.x,mouse.y) }
                                        onReleased: root.controller.finishInteraction()
                                    }
                                }
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    font.pixelSize: 9; color: "#93a3b5"
                                    text: Math.round(gradeWheelColumn.hue)+"° / "+Math.round(gradeWheelColumn.saturation)+"%"
                                }
                            }
                        }
                    }
                    Repeater {
                        model: 3
                        delegate: ColumnLayout {
                            required property int index
                            Layout.fillWidth: true; spacing: 2
                            enabled: root.controller.hasImage && root.controller.gradingEnabled
                            AdjustmentSlider {
                                Layout.fillWidth: true
                                label: [root.t("阴影色相","Shadow hue"),root.t("中间调色相","Mid hue"),
                                        root.t("高光色相","Highlight hue")][index]
                                from: 0; to: 360; decimals: 0
                                value: root.controller.gradingHue[index]
                                onEdited: root.controller.setGradingControl(index,0,newValue)
                            }
                            AdjustmentSlider {
                                Layout.fillWidth: true
                                label: [root.t("阴影饱和度","Shadow sat"),root.t("中间调饱和度","Mid sat"),
                                        root.t("高光饱和度","Highlight sat")][index]
                                from: 0; to: 100; decimals: 0
                                value: root.controller.gradingSaturation[index]
                                onEdited: root.controller.setGradingControl(index,1,newValue)
                            }
                        }
                    }
                    AdjustmentSlider {
                        objectName: "gradingBalanceSlider"; Layout.fillWidth: true
                        enabled: root.controller.hasImage && root.controller.gradingEnabled
                        label: root.t("明暗平衡","Balance"); from: -100; to: 100; decimals: 0
                        value: root.controller.gradingBalance
                        onEdited: root.controller.gradingBalance = newValue
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: "#738293"
                        text: root.t("三段平滑权重，Oklab 色彩分级；负平衡偏向阴影，正平衡偏向高光。位于黑白混合之后、输出色域转换之前。",
                                     "Smooth three-zone Oklab grading; negative balance favors shadows, positive favors highlights. After B&W, before output transform.")
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        objectName: "vignetteToggle"
                        Layout.fillWidth: true; font.bold: true
                        text: (root.vignetteExpanded ? "▾ " : "▸ ") + root.t("暗角", "VIGNETTE")
                        onClicked: root.vignetteExpanded = !root.vignetteExpanded
                    }
                    Button {
                        objectName: "vignetteReset"; text: root.t("重置", "Reset")
                        enabled: root.controller.hasImage; onClicked: root.controller.resetVignette()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 6; visible: root.vignetteExpanded
                    AdjustmentSlider { Layout.fillWidth: true; sliderObjectName: "vignetteAmountSlider"; label: root.t("强度 EV", "Amount EV"); from: -3; to: 3; decimals: 2; value: root.controller.vignetteAmount; onEdited: function(newValue) { root.controller.vignetteAmount = newValue } }
                    AdjustmentSlider { Layout.fillWidth: true; sliderObjectName: "vignetteMidpointSlider"; label: root.t("中点", "Midpoint"); from: 0; to: .95; decimals: 2; value: root.controller.vignetteMidpoint; onEdited: function(newValue) { root.controller.vignetteMidpoint = newValue } }
                    AdjustmentSlider { Layout.fillWidth: true; sliderObjectName: "vignetteFeatherSlider"; label: root.t("羽化", "Feather"); from: .01; to: 1; decimals: 2; value: root.controller.vignetteFeather; onEdited: function(newValue) { root.controller.vignetteFeather = newValue } }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: "#738293"
                        text: root.t("裁切后椭圆暗角；负值压暗边缘，正值提亮。在线性工作空间、明暗映射之前执行。",
                            "Post-crop ellipse; negative darkens edges, positive brightens. Applied in linear working space before tone mapping.")
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        objectName: "linearMaskToggle"; Layout.fillWidth: true; font.bold: true
                        text: (root.linearMaskExpanded ? "▾ " : "▸ ") + root.t("局部线性蒙版 (1)", "LINEAR MASK (1)")
                        onClicked: root.linearMaskExpanded = !root.linearMaskExpanded
                    }
                    Button {
                        objectName: "linearMaskReset"; text: root.t("重置", "Reset")
                        enabled: root.controller.hasImage
                        onClicked: root.controller.resetLinearMask()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 6; visible: root.linearMaskExpanded
                    CheckBox {
                        objectName: "linearMaskEnable"; Layout.fillWidth: true
                        text: root.t("启用渐变曝光", "Enable graduated exposure")
                        checked: root.controller.linearMask.enabled
                        enabled: root.controller.hasImage
                        onToggled: root.controller.setLinearMaskEnabled(checked)
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "linearMaskExposureSlider"
                        label: root.t("局部曝光 EV", "Local exposure EV"); from: -3; to: 3
                        decimals: 2; value: root.controller.linearMask.exposure
                        onEdited: function(v) { root.controller.setLinearMaskParameter("exposure",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "linearMaskCenterSlider"
                        label: root.t("位置", "Position"); from: 0; to: 1
                        decimals: 2; value: root.controller.linearMask.center
                        onEdited: function(v) { root.controller.setLinearMaskParameter("center",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "linearMaskFeatherSlider"
                        label: root.t("羽化宽度", "Feather width"); from: .02; to: 1
                        decimals: 2; value: root.controller.linearMask.feather
                        onEdited: function(v) { root.controller.setLinearMaskParameter("feather",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "linearMaskAngleSlider"
                        label: root.t("方向 (°)", "Angle (°)"); from: -180; to: 180
                        decimals: 0; value: root.controller.linearMask.angle
                        onEdited: function(v) { root.controller.setLinearMaskParameter("angle",v) }
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: "#738293"
                        text: root.t("手动线性渐变：0° 向下，90° 向右。以裁切/旋转后的完整照片为坐标基准；支持画布手柄调整位置、旋转和两侧羽化（Shift 精调）；颜色/亮度选择及多个同类渐变尚未实现。",
                                     "One linear gradient: 0° down, 90° right. Anchored to the complete post-crop/rotation image; canvas handles edit position, rotation and both feather boundaries (Shift for precision); color/luma selection and multiple gradients are not yet available.")
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        objectName: "radialMaskToggle"; Layout.fillWidth: true; font.bold: true
                        text: (root.radialMaskExpanded ? "▾ " : "▸ ") + root.t("椭圆／径向蒙版 (1)", "RADIAL MASK (1)")
                        onClicked: root.radialMaskExpanded = !root.radialMaskExpanded
                    }
                    Button {
                        objectName: "radialMaskReset"; text: root.t("重置", "Reset")
                        enabled: root.controller.hasImage
                        onClicked: root.controller.resetRadialMask()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 6; visible: root.radialMaskExpanded
                    CheckBox {
                        objectName: "radialMaskEnable"; Layout.fillWidth: true
                        text: root.t("启用局部曝光", "Enable local exposure")
                        checked: root.controller.radialMask.enabled
                        enabled: root.controller.hasImage
                        onToggled: root.controller.setRadialMaskEnabled(checked)
                    }
                    CheckBox {
                        objectName: "radialMaskInvert"; Layout.fillWidth: true
                        text: root.t("反转：调整椭圆外侧", "Invert: adjust outside oval")
                        checked: root.controller.radialMask.inverted
                        enabled: root.controller.hasImage
                        onToggled: root.controller.setRadialMaskInverted(checked)
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "radialMaskExposureSlider"
                        label: root.t("曝光 EV", "Exposure EV"); from: -3; to: 3; decimals: 2
                        value: root.controller.radialMask.exposure
                        onEdited: function(v) { root.controller.setRadialMaskParameter("exposure",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "radialMaskCenterXSlider"
                        label: root.t("中心 X", "Center X"); from: 0; to: 1; decimals: 2
                        value: root.controller.radialMask.centerX
                        onEdited: function(v) { root.controller.setRadialMaskParameter("centerX",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "radialMaskCenterYSlider"
                        label: root.t("中心 Y", "Center Y"); from: 0; to: 1; decimals: 2
                        value: root.controller.radialMask.centerY
                        onEdited: function(v) { root.controller.setRadialMaskParameter("centerY",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "radialMaskRadiusXSlider"
                        label: root.t("横向半径", "Radius X"); from: .05; to: 1; decimals: 2
                        value: root.controller.radialMask.radiusX
                        onEdited: function(v) { root.controller.setRadialMaskParameter("radiusX",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "radialMaskRadiusYSlider"
                        label: root.t("纵向半径", "Radius Y"); from: .05; to: 1; decimals: 2
                        value: root.controller.radialMask.radiusY
                        onEdited: function(v) { root.controller.setRadialMaskParameter("radiusY",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "radialMaskFeatherSlider"
                        label: root.t("羽化", "Feather"); from: .02; to: 1; decimals: 2
                        value: root.controller.radialMask.feather
                        onEdited: function(v) { root.controller.setRadialMaskParameter("feather",v) }
                    }
                    AdjustmentSlider {
                        Layout.fillWidth: true; sliderObjectName: "radialMaskAngleSlider"
                        label: root.t("椭圆旋转 (°)", "Ellipse rotation (°)"); from: -180; to: 180; decimals: 0
                        value: root.controller.radialMask.angle
                        onEdited: function(v) { root.controller.setRadialMaskParameter("angle",v) }
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: "#738293"
                        text: root.t("一个独立椭圆蒙版，可调整内部或外部。基于裁切/旋转后的整张照片，无 AI；支持旋转椭圆及画布两侧羽化手柄（Shift 精调）；多个同类渐变仍待支持。",
                                     "One independent oval mask, inside or outside. Anchored to the entire post-crop/rotation photo, no AI. Rotated oval and two-sided feather handles support Shift precision; multiple radial gradients remain future work.")
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton { id: brushPanelHeader; objectName: "brushPanelToggle"; text: (root.brushExpanded ? "▾ " : "▸ ")+root.t("笔刷局部曝光","BRUSH EXPOSURE"); font.bold: true; onClicked: root.brushExpanded=!root.brushExpanded }
                    Item { Layout.fillWidth: true }
                    Button { text: root.t("清空","Clear"); enabled: root.controller.brushLayers.length>0; onClicked: root.controller.executeEditCommand({command:"mask.brush.reset"}) }
                }
                ColumnLayout {
                    visible: root.brushExpanded; Layout.fillWidth: true; spacing: 6; enabled: root.brushEditor !== null
                    ComboBox {
                        objectName: "brushLayerSelector"; Layout.fillWidth: true; model: root.brushOptions; textRole: "label"
                        currentIndex: { if (!root.brushEditor) return 0; for(let i=0;i<root.brushOptions.length;++i)if(root.brushOptions[i].id===root.brushEditor.layerId)return i; return 0 }
                        onActivated: function(index) { root.brushEditor.layerId=root.brushOptions[index].id }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox { objectName: "brushLayerEnabled"; text: root.t("启用","Enabled"); enabled: root.currentBrush !== null; checked: root.currentBrush ? root.currentBrush.enabled : false; onClicked: if(root.currentBrush)root.controller.setBrushLayerEnabled(root.currentBrush.id,checked) }
                        Item { Layout.fillWidth: true }
                        Button { objectName: "removeLastBrushStroke"; text: root.t("删除末笔","Remove last stroke"); enabled: root.currentBrush !== null; onClicked: if(root.currentBrush)root.controller.removeLastBrushStroke(root.currentBrush.id) }
                        Button { objectName: "removeBrushLayer"; text: root.t("删除图层","Delete layer"); enabled: root.currentBrush !== null; onClicked: if(root.currentBrush)root.controller.removeBrushLayer(root.currentBrush.id) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            objectName: "brushStrokeSelector"; Layout.fillWidth: true
                            enabled: root.controller.currentSourceAvailable && root.currentBrush !== null
                            model: {
                                let options=[root.t("选择笔触以预览位置","Select stroke to preview")]
                                for(let i=0;root.currentBrush && i<root.currentBrush.strokes;++i) options.push(root.t("笔触 ","Stroke ")+(i+1))
                                return options
                            }
                            currentIndex: root.brushEditor && root.brushEditor.strokeSelection.token ? root.brushEditor.strokeSelection.index+1 : 0
                            onActivated: function(index) {
                                root.brushEditor.strokeSelection=index>0 && root.currentBrush ? root.controller.brushStrokeSelection(root.currentBrush.id,index-1) : ({})
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: root.t("黄色线条显示所选整笔的路径；删除后可撤销。","The yellow line shows the selected whole stroke's path. Deletion can be undone.")
                        }
                        Button {
                            objectName: "removeSelectedBrushStroke"; text: root.t("删除所选笔触","Delete stroke")
                            enabled: root.controller.currentSourceAvailable && root.currentBrush !== null && root.brushEditor && !!root.brushEditor.strokeSelection.token
                            onClicked: {
                                const selection=root.brushEditor.strokeSelection
                                root.controller.removeBrushStroke(root.currentBrush.id,selection.index,selection.token)
                                root.brushEditor.strokeSelection=({})
                            }
                        }
                    }
                    AdjustmentSlider {
                        id: brushExposureControl
                        Layout.fillWidth: true; sliderObjectName: "brushExposureSlider"; label: root.t("局部曝光","Local EV"); from: -3; to: 3; decimals: 2
                        value: root.currentBrush ? root.currentBrush.exposure : root.brushEditor ? root.brushEditor.exposure : 1
                        onEdited: function(v) { if(root.currentBrush)root.controller.setBrushLayerExposure(root.currentBrush.id,v);else if(root.brushEditor)root.brushEditor.exposure=v }
                    }
                    RowLayout { Layout.fillWidth: true; Label { text: root.t("半径","Radius"); Layout.preferredWidth: 74 }
                        Slider { objectName: "brushRadiusSlider"; Layout.fillWidth: true; from: .002; to: .5; value: root.brushEditor ? root.brushEditor.radius : .05; onMoved: root.brushEditor.radius=value }
                        Label { text: root.brushEditor ? (root.brushEditor.radius*100).toFixed(1)+"%" : ""; Layout.preferredWidth: 42 }
                    }
                    RowLayout { Layout.fillWidth: true; Label { text: root.t("硬度","Hardness"); Layout.preferredWidth: 74 }
                        Slider { objectName: "brushHardnessSlider"; Layout.fillWidth: true; from: 0; to: 1; value: root.brushEditor ? root.brushEditor.hardness : .6; onMoved: root.brushEditor.hardness=value }
                        Label { text: root.brushEditor ? Math.round(root.brushEditor.hardness*100)+"%" : ""; Layout.preferredWidth: 42 }
                    }
                    RowLayout { Layout.fillWidth: true; Label { text: root.t("不透明度","Opacity"); Layout.preferredWidth: 74 }
                        Slider { objectName: "brushOpacitySlider"; Layout.fillWidth: true; from: 0; to: 1; value: root.brushEditor ? root.brushEditor.opacity : 1; onMoved: root.brushEditor.opacity=value }
                        Label { text: root.brushEditor ? Math.round(root.brushEditor.opacity*100)+"%" : ""; Layout.preferredWidth: 42 }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: "#738293"; text: root.t("选择画布上的“绘制笔刷”，拖动后松开应用。每笔可独立撤销；Esc 取消当前笔触。","Choose Paint brush on the canvas; drag and release to apply. Each stroke has one Undo step; Esc cancels it.") }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        text: (root.mixerExpanded ? "▾ " : "▸ ") + root.t("HSL 颜色混合器", "COLOR MIXER")
                        onClicked: root.mixerExpanded = !root.mixerExpanded
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: mixerMode; Layout.preferredWidth: 105
                        model: [root.t("色相", "Hue"), root.t("饱和度", "Sat"), root.t("明度", "Luma")]
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.mixerExpanded
                Repeater {
                    model: 8
                    delegate: AdjustmentSlider {
                        required property int index
                        Layout.fillWidth: true
                        label: [root.t("红色", "Red"), root.t("橙色", "Orange"), root.t("黄色", "Yellow"), root.t("绿色", "Green"), root.t("青色", "Aqua"), root.t("蓝色", "Blue"), root.t("紫色", "Purple"), root.t("洋红", "Magenta")][index]
                        value: mixerMode.currentIndex === 0 ? root.controller.hslHue[index] : mixerMode.currentIndex === 1 ? root.controller.hslSaturation[index] : root.controller.hslLuminance[index]
                        onEdited: root.controller.setColorMix(index, mixerMode.currentIndex, newValue)
                    }
                }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        text: (root.curveExpanded ? "▾ " : "▸ ") + root.t("曲线", "TONE CURVE")
                        onClicked: root.curveExpanded = !root.curveExpanded
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    ComboBox { id: curveChannel; Layout.preferredWidth: 110; model: [root.t("主曲线", "Master"), "Red", "Green", "Blue"] }
                    Button { text: root.t("重置", "Reset"); onClicked: root.controller.resetCurve(curveChannel.currentIndex) }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.curveExpanded
                CurveEditor {
                    Layout.fillWidth: true; Layout.preferredHeight: 160
                    channel: curveChannel.currentIndex
                    values: curveChannel.currentIndex === 0 ? root.controller.masterCurve : curveChannel.currentIndex === 1 ? root.controller.redCurve : curveChannel.currentIndex === 2 ? root.controller.greenCurve : root.controller.blueCurve
                    onPointEdited: function(point, value) { root.controller.setCurvePoint(channel, point, value) }
                }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
                ToolButton {
                    Layout.fillWidth: true
                    text: (root.exifExpanded ? "▾ " : "▸ ") + root.t("照片信息 / EXIF", "PHOTO INFO / EXIF")
                    onClicked: root.exifExpanded = !root.exifExpanded
                    font.bold: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.exifExpanded
                GridLayout {
                    Layout.fillWidth: true; columns: 2; columnSpacing: 10; rowSpacing: 5
                    Label { text: root.t("相机", "Camera"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.cameraName(); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: root.t("镜头", "Lens"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("lens"); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: root.t("快门", "Shutter"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("shutter"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: root.t("光圈", "Aperture"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("aperture"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: "ISO"; color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("iso"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: root.t("焦距", "Focal Length"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("focalLength"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { text: root.t("拍摄时间", "Captured"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("captureTime"); color: "#c4cfda"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: root.t("尺寸", "Dimensions"); color: "#748394"; font.pixelSize: 10 }
                    Label { text: root.meta("pixelWidth") + " × " + root.meta("pixelHeight"); color: "#c4cfda"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.t("RAW 深度", "RAW Depth"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.meta("bitDepth") + "-bit"; color: "#88ead0"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.t("工作空间", "Working Space"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.meta("workingSpace"); color: "#88ead0"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.t("去马赛克", "Demosaic"); color: "#748394"; font.pixelSize: 10 }
                    Label { visible: root.controller.currentIsRaw; text: root.meta("demosaic"); color: "#88ead0"; font.pixelSize: 10 }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.t("处理顺序：RAW → Camera WB/Matrix → Linear ProPhoto → HSL/Color → Curves → ICC sRGB Preview", "Graph: RAW → Camera WB/Matrix → Linear ProPhoto → HSL/Color → Curves → ICC sRGB Preview")
                    wrapMode: Text.WordWrap; color: "#627180"; font.pixelSize: 10; Layout.bottomMargin: 18
                }
                }
            }
        }
    }
}
