import QtQuick
import QtQuick.Controls
Label {
    id: root
    required property var controller
    required property string slot
    property var detail: controller.state.errors[slot] || ({})
    visible: Object.keys(detail).length > 0
    text: (detail.component || "") + " · " + (detail.status || "") + "\n" + (detail.message || "")
    color: "#fa9298"; wrapMode: Text.Wrap
}
