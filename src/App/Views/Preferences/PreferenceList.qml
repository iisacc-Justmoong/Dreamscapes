pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

// A single composed settings section, or model options, in the LVRS list viewport.
LV.List {
    id: root
    default property alias view: root.itemDelegate
    items: [({})]
    footerVisible: false
    minimumListHeight: 0
    itemHeight: 0
    listWidth: 0
    backgroundColor: "transparent"
    property var scrollViewport: null
    readonly property real contentY: scrollViewport ? scrollViewport.contentY : 0

    function reveal(row) {
        if (!row) return
        // LV.List owns scrolling. Resolve the nearest scroll interface from its row.
        let target = row.parent
        while (target && !("contentY" in target && "contentHeight" in target))
            target = target.parent
        if (!target) return
        scrollViewport = target
        const top = row.mapToItem(target, 0, 0).y
        const offset = top < 0 ? top : top + row.height > target.height ? top + row.height - target.height : 0
        target.contentY = Math.max(0, Math.min(target.contentHeight - target.height, target.contentY + offset))
    }

    function revealNamed(name) {
        function find(node) {
            if (node.objectName === name) return node
            for (const child of node.children) {
                const found = find(child)
                if (found) return found
            }
            return null
        }
        reveal(find(root))
    }
}
