import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var controller
    property var recorded: controller.sonyLook
    property var applied: controller.lookState
    property var recordedParams: recorded.parameters || ({})
    property var appliedParams: applied.parameters || ({})
    property var fields: ["contrast","highlights","shadows","fade","saturation","sharpness","sharpnessRange","clarity"]
    property var codes: ["ST","PT","NT","VV","VV2","FL","IN","SH","BW","SE","FL2","FL3"]
    function t(zh,en) { return controller.language === "zh_CN" ? zh : en }
    function fieldName(i) { return [t("对比度","Contrast"),t("高光","Highlights"),t("阴影","Shadows"),t("褪色","Fade"),t("饱和度","Saturation"),t("锐度","Sharpness"),t("锐度范围","Sharpness range"),t("清晰度","Clarity")][i] }
    spacing: 8
    Label { text: root.t("索尼外观 · 拍摄记录", "SONY LOOK · AS SHOT"); color: "#8e9aa8"; font.bold: true }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#dce5ef"
        text: (root.recorded.generation === "creative-look" ? "Creative Look" : root.recorded.generation === "creative-style" ? "Creative Style" : root.t("类型未确认", "Generation unconfirmed"))
              + " · " + (root.recorded.code || root.t("未记录 / 未识别", "Missing / unknown"))
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#d6b985"; font.pixelSize: 10
        text: root.recorded.status === "conflict" ? root.t("字段冲突：已禁止自动套用。", "Conflicting fields: automatic application disabled.") : root.recorded.autoEligible ? root.t("已识别；下方记录值不会随编辑改变。", "Recognized; recorded values remain immutable.") : root.t("没有足够依据自动套用；保留原始字段，不猜测。", "Insufficient evidence for automatic application; no guessed values.")
    }
    GridLayout {
        columns: 2; Layout.fillWidth: true
        Repeater { model: 8
            Label {
                required property int index
                Layout.fillWidth: true; font.pixelSize: 11; color: "#aebccc"
                text: root.fieldName(index) + ": " + (root.recordedParams[root.fields[index]] !== undefined ? root.recordedParams[root.fields[index]] : root.t("未记录", "not recorded"))
            }
        }
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#778594"; font.pixelSize: 10
        text: root.t("自定义槽位：未解码。字段名称与原始值保存在诊断记录中。", "Custom slot not decoded. Source tags and raw values are retained in diagnostics.") }
    Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
    Label { text: root.t("当前编辑外观", "CURRENT EDITING LOOK"); color: "#8e9aa8"; font.bold: true }
    RowLayout {
        Layout.fillWidth: true; enabled: root.controller.hasImage
        ComboBox {
            objectName: "lookMode"; Layout.fillWidth: true
            model: [root.t("关闭", "Off"), root.t("按拍摄设置（近似）", "As shot (approximate)"), root.t("手动 / 导入", "Manual / imported")]
            currentIndex: root.applied.mode === "off" ? 0 : root.applied.mode === "as-shot" ? 1 : 2
            onActivated: root.controller.setLookMode(["off","as-shot","manual"][currentIndex])
        }
        ComboBox {
            objectName: "lookCode"; Layout.preferredWidth: 78; model: root.codes
            currentIndex: root.codes.indexOf(root.applied.code)
            onActivated: root.controller.setLookCode(currentText)
        }
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#d6b985"; font.pixelSize: 11
        text: root.applied.mode === "calibrated"
              ? root.t("参考拟合 / 导入 LUT · 非索尼官方配置。显示 sRGB 域，不能恢复超出参考色域的信息。", "Reference fit / imported LUT, not a Sony profile. Display-sRGB domain; no recovery of colors outside the reference gamut.")
              : root.t("JixelLight 独立近似预设 · 未实机标定，不保证与机内 JPEG 一致。", "Independent JixelLight approximations, not camera-calibrated or guaranteed to match in-camera JPEGs.")
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10; color: "#d6b985"
        visible: root.applied.evidence && root.applied.evidence.kind === "multi-scene-empirical-fit"
        text: !visible ? "" : root.t("多场景经验匹配 · 仅在记录的独立场景验证；颜色节点覆盖 ", "Empirical multi-scene fit · validated only on the recorded held-out scenes; color-node coverage ")
              + (100*Number(root.applied.evidence.coveredNodeFraction || 0)).toFixed(1) + "%"
              + root.t("。不是通用相机标定，换机型/光源/外观需重新验证。", ". Not universal camera calibration; revalidate on other cameras, illuminants or looks.")
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: !!root.applied.error; text: root.applied.error || ""; color: "#ffb066" }
    AdjustmentSlider { Layout.fillWidth: true; label: root.t("外观强度", "Look strength"); from: 0; to: 100; value: (root.applied.strength || 0)*100; enabled: root.controller.hasImage; onEdited: root.controller.setLookStrength(newValue/100) }
    CheckBox { id: fine; text: root.t("外观微调（与拍摄记录分离）", "Fine adjustments (independent of recorded values)"); font.pixelSize: 11 }
    ColumnLayout {
        visible: fine.checked; Layout.fillWidth: true; enabled: root.controller.hasImage
        Repeater { model: 8
            AdjustmentSlider {
                required property int index
                Layout.fillWidth: true; label: root.fieldName(index); decimals: 0
                from: index === 6 ? 1 : [0,1,2,4].indexOf(index) >= 0 ? -9 : 0
                to: index === 6 ? 5 : 9
                value: root.appliedParams[root.fields[index]] !== undefined ? root.appliedParams[root.fields[index]] : index === 6 ? 3 : 0
                enabled: !(index === 4 && (root.applied.code === "BW" || root.applied.code === "SE"))
                onEdited: root.controller.setLookParameter(root.fields[index], newValue)
            }
        }
    }
    CheckBox { objectName: "cameraReferenceToggle"; text: root.t("并排对照相机 / 参考图", "Compare camera / reference image"); checked: root.controller.showCameraReference; enabled: root.controller.hasImage; onToggled: root.controller.showCameraReference=checked }
    RowLayout {
        Layout.fillWidth: true
        Button { Layout.fillWidth: true; text: root.t("选择参考 JPEG", "Reference JPEG"); enabled: root.controller.hasImage; onClicked: root.controller.openReferenceDialog() }
        Button { objectName: "fitLook"; Layout.fillWidth: true; text: root.t("拟合本照片", "Match this image"); enabled: root.controller.currentIsRaw && root.controller.previewReady && !!root.controller.cameraReferenceUrl && !root.controller.referenceBusy && !root.controller.calibrationBusy; onClicked: root.controller.calibrateFromReference() }
    }
    RowLayout { visible: root.controller.calibrationBusy; Layout.fillWidth: true
        BusyIndicator { running: visible; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
        Label { text: root.t("后台拟合与留出区域验证…", "Fitting and validating held-out areas…"); Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11 }
        Button { text: root.t("取消", "Cancel"); onClicked: root.controller.cancelCalibration() }
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#aebccc"; font.pixelSize: 10
        text: root.controller.calibrationReport.heldoutRmseAfter !== undefined
            ? "sRGB RMSE " + Number(root.controller.calibrationReport.heldoutRmseBefore).toFixed(4) + " → " + Number(root.controller.calibrationReport.heldoutRmseAfter).toFixed(4) + root.t(" · 仅针对本照片，不是相机通用标定", " · this image only, not universal camera calibration") : root.t("拟合需要同次拍摄、未裁剪、方向一致的参考图。它拟合颜色，不逆推出锐化或降噪算法。", "Matching requires the same uncropped, correctly oriented shot. It fits color, not the camera’s sharpening or noise-reduction algorithm.") }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#ffb066"; font.pixelSize: 10; visible: !!root.controller.calibrationReport.error; text: root.controller.calibrationReport.error || "" }
    RowLayout {
        Layout.fillWidth: true; enabled: root.controller.hasImage
        Button { Layout.fillWidth: true; text: root.t("导入外观 / LUT", "Import look / LUT"); onClicked: root.controller.openLookProfileDialog() }
        Button { Layout.fillWidth: true; text: root.t("保存外观", "Save look"); enabled: root.applied.active; onClicked: root.controller.saveLookProfileDialog() }
    }
    Rectangle { Layout.fillWidth: true; height: 1; color: "#29333e" }
}
