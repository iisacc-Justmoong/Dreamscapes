import QtQuick
import LVRS 1.0 as LV

LV.Sheet {
    id: sheet
    cornerRadius: 0
    background: PanelFrame {
        objectName: "panelFrame_" + sheet.objectName
        z: -1
        color: sheet.surfaceColor
    }
}
