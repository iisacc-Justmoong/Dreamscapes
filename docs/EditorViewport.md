# Editor viewport behavior

The editor canvas is centered in the actual canvas viewport between the sidebar, tool dock and visible editor controls. The bottom instructional overlay has been removed; it no longer covers the canvas or the dock.

Finite canvases use an axis-specific camera boundary. For viewport length `V` and scaled canvas length `L`, a fitting axis (`L <= V`) is locked to `pan = (V - L) / 2`. An overflowing axis permits `V - L <= pan <= 0`, so either edge can be reached without dragging the canvas beyond the viewport and exposing extra space. A canvas that fits in both axes stays centered and cannot be panned. If only one axis overflows, only that axis can move.

`EditorCanvasViewport.cpp` owns this product presentation rule. It constrains the existing iiSharedCanvas viewport notifications and viewport geometry changes. The same rule therefore applies to direct pan/zoom setters, middle-button drag, cursor-anchored wheel zoom, Fit, Reset and document remounting. Zooming out to a fitting size automatically recenters each fitting axis. The rule does not edit document content or replace the SDK renderer. Infinite canvases retain unrestricted navigation.

`Dreamscapes.EditorCanvas` covers fitting, exact-fit, width-only and height-only overflow, both-axis overflow, fractional scaling, edge clamping, zoom-out recentering, resize/reset, unchanged document revision and the infinite-canvas boundary. `editorCanvasPanAndZoomStayInsideViewport` exercises actual wheel and middle-button events through the editor window. The existing layout, dock resize, document/project mounting and editing/persistence tests remain integration coverage.
