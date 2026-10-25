import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var controller
    Layout.fillWidth: true; spacing: 14
    Label { text: "Define the physical target"; font.pixelSize: 24; font.bold: true }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7; text: "Use the board you have. Dimensions and dictionary are editable; the daemon validates support." }
    ArtifactSelector { controller: root.controller; slot: "target"; title: "Resume with an existing Target" }
    ColumnLayout {
        visible: !!root.controller.state.target.id; Layout.fillWidth: true
        property var selectedTarget: root.controller.state.target
        Label { text: "Selected · " + (root.controller.state.target.pattern || "") + " · revision " + (root.controller.state.target.revision || ""); font.bold: true }
        Label { text: (root.controller.state.target.squaresX || 0) + " × " + (root.controller.state.target.squaresY || 0) + " squares · " + (root.controller.state.target.squareMm || 0) + " mm nominal" }
        Label { visible: root.controller.state.target.pattern === "charuco"; text: (root.controller.state.target.dictionary || "") + " · " + (root.controller.state.target.layout || ""); Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
        Button { id: targetDetails; text: "Target details"; checkable: true }
        Label {
            visible: targetDetails.checked; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; opacity: 0.7
            text: "Artifact · " + (root.controller.state.target.id || "") + "\nLogical ID · " + (root.controller.state.target.logicalId || "") + "\nMeasured active width / height · " + (root.controller.state.target.measuredWidth === undefined ? "not supplied" : root.controller.state.target.measuredWidth + " mm") + " / " + (root.controller.state.target.measuredHeight === undefined ? "not supplied" : root.controller.state.target.measuredHeight + " mm") + "\nProvenance · " + (root.controller.state.target.provenancePresent ? "present" : "not supplied")
        }
    }
    Label { text: "Create a new target"; font.bold: true }
    ComboBox { id: pattern; model: ["ChArUco", "Checkerboard"]; Layout.fillWidth: true }
    GridLayout {
        columns: 2; Layout.fillWidth: true; columnSpacing: 16; rowSpacing: 10
        Label { text: "Squares X" }
        TextField { id: sx; text: "8"; Layout.fillWidth: true }
        Label { text: "Squares Y" }
        TextField { id: sy; text: "6"; Layout.fillWidth: true }
        Label { text: "Nominal square size (mm)" }
        TextField { id: square; text: "40"; Layout.fillWidth: true }
        Label { text: "Marker size (mm)"; visible: pattern.currentIndex === 0 }
        TextField { id: marker; text: "25"; visible: pattern.currentIndex === 0; Layout.fillWidth: true }
        Label { text: "Dictionary"; visible: pattern.currentIndex === 0 }
        ComboBox { id: dictionary; editable: true; model: ["DICT_6X6_250", "DICT_4X4_50", "DICT_5X5_100", "DICT_7X7_250"]; visible: pattern.currentIndex === 0; Layout.fillWidth: true }
        Label { text: "Physical layout"; visible: pattern.currentIndex === 0 }
        ComboBox {
            id: layout; visible: pattern.currentIndex === 0; Layout.fillWidth: true
            textRole: "label"; valueRole: "value"
            model: [{label: "Black square at origin", value: "black_square_at_origin"}, {label: "White at origin (even rows)", value: "white_square_at_origin_even_rows"}]
        }
    }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Layout must match the physical print. Form defaults are starting suggestions, not a recommended Mantis board."; opacity: 0.7 }
    CheckBox { id: measured; text: "Use measured active grid dimensions" }
    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Measure the ACTIVE GRID, excluding paper margins and substrate. Enter both extents in mm."; opacity: 0.7; visible: measured.checked }
    GridLayout {
        visible: measured.checked; columns: 2; Layout.fillWidth: true
        Label { text: "Measured active width (mm)" }
        TextField { id: mw; Layout.fillWidth: true }
        Label { text: "Measured active height (mm)" }
        TextField { id: mh; Layout.fillWidth: true }
    }
    Button { id: advanced; checkable: true; text: checked ? "Hide measurement provenance" : "Advanced · measurement provenance" }
    ColumnLayout {
        visible: advanced.checked; Layout.fillWidth: true
        CheckBox { id: provenance; text: "Include measurement provenance" }
        Label { text: "Unchecked preserves absent provenance; checked with blank fields preserves present empty provenance."; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7 }
        GridLayout {
            enabled: provenance.checked; columns: 2; Layout.fillWidth: true
            Label { text: "Width uncertainty (mm)" }
            TextField { id: wu; Layout.fillWidth: true }
            Label { text: "Height uncertainty (mm)" }
            TextField { id: hu; Layout.fillWidth: true }
            Label { text: "Instrument" }
            TextField { id: instrument; Layout.fillWidth: true }
            Label { text: "Note" }
            TextField { id: note; Layout.fillWidth: true }
        }
    }
    Button {
        text: root.controller.state.pending.target ? "Creating target…" : "Create Target"; enabled: !root.controller.state.pending.target
        onClicked: {
            let f = {pattern: pattern.currentIndex === 0 ? "charuco" : "checkerboard", squaresX: sx.text, squaresY: sy.text, squareMm: square.text,
                markerMm: marker.text, dictionary: dictionary.editText, layout: layout.currentValue, provenancePresent: provenance.checked}
            if (measured.checked) { f.measuredWidth = mw.text; f.measuredHeight = mh.text }
            if (provenance.checked) {
                if (wu.text.length) f.widthUncertainty = wu.text
                if (hu.text.length) f.heightUncertainty = hu.text
                if (instrument.text.length) f.instrument = instrument.text
                if (note.text.length) f.note = note.text
            }
            root.controller.createTarget(f)
        }
    }
    StageError { controller: root.controller; slot: "target"; Layout.fillWidth: true }
}
