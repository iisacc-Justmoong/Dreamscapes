.pragma library

// Figma Dreamscapes 165:3042. Stable field IDs keep independent tool drafts.
var tools = [
    {"key": "elements", "title": "Elements", "number": 1, "figmaNode": "196:3034", "selector": {"id": "selector", "type": "Choices", "label": "Shape", "options": ["Rectangle", "Ellipse", "Polygon", "Line / Arrow"], "desktopColumns": 4, "initial": "Rectangle"}, "fields": [
        {"id": "field-0", "type": "Dimensions", "label": "Size", "initial": [1080, 1080]},
        {"id": "field-1", "type": "Slider", "label": "Corner radius", "initial": 24.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-2", "type": "Choices", "label": "Fill", "desktopColumns": 4, "initial": "Solid", "options": ["Solid", "Gradient", "None", "Image"]},
        {"id": "field-3", "type": "Toggle", "label": "Constrain proportions", "initial": true, "description": "비율을 고정한다"},
        {"id": "field-4", "type": "Slider", "label": "Arc sweep", "initial": 360.0, "minimum": 0, "maximum": 360, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-5", "type": "Slider", "label": "Inner radius", "initial": 0.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-6", "type": "Choices", "label": "Stroke location", "desktopColumns": 3, "initial": "Inside", "options": ["Inside", "Center", "Outside"]},
        {"id": "field-7", "type": "Slider", "label": "Sides", "initial": 3.0, "minimum": 3, "maximum": 64, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-8", "type": "Slider", "label": "Corner smoothing", "initial": 18.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-9", "type": "Toggle", "label": "Star mode", "initial": true, "description": "내부 반지름을 사용한다"},
        {"id": "field-10", "type": "Slider", "label": "Stroke width", "initial": 4.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-11", "type": "Segmented", "label": "Cap", "desktopColumns": 3, "initial": "Round", "options": ["Round", "Square", "Butt"]},
        {"id": "field-12", "type": "Choices", "label": "Start", "desktopColumns": 3, "initial": "None", "options": ["None", "Dot", "Arrow"]},
        {"id": "field-13", "type": "Choices", "label": "End", "desktopColumns": 3, "initial": "Arrow", "options": ["Arrow", "Dot", "None"]},
        {"id": "field-14", "type": "Slider", "label": "Opacity", "initial": 100.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-15", "type": "Segmented", "label": "Blend", "desktopColumns": 3, "initial": "Normal", "options": ["Normal", "Multiply", "Screen"]},
        {"id": "field-16", "type": "Color", "label": "Fill color", "initial": "#8B7CFF"},
        {"id": "field-17", "type": "Color", "label": "Stroke color", "initial": "#FFFFFF"},
    ]},
    {"key": "text", "title": "Text", "number": 2, "figmaNode": "196:3413", "selector": {"id": "selector", "type": "Choices", "label": "Role", "options": ["Free Text", "Caption", "Header Title", "Callout", "Footer"], "desktopColumns": 4, "initial": "Free Text"}, "fields": [
        {"id": "field-0", "type": "Field", "label": "Free Text · Content", "initial": "Enter your text…"},
        {"id": "field-1", "type": "Choices", "label": "Typeface", "desktopColumns": 3, "initial": "Pretendard", "options": ["Pretendard", "Serif", "Mono"]},
        {"id": "field-2", "type": "Slider", "label": "Font size", "initial": 48.0, "minimum": 1, "maximum": 512, "step": 1, "unit": "pt", "decimals": 0},
        {"id": "field-3", "type": "Segmented", "label": "Alignment", "desktopColumns": 3, "initial": "Left", "options": ["Left", "Center", "Right"]},
        {"id": "field-4", "type": "Field", "label": "Time range", "initial": "00:02.4 — 00:06.8"},
        {"id": "field-5", "type": "Segmented", "label": "Placement", "desktopColumns": 3, "initial": "Bottom", "options": ["Bottom", "Center", "Top"]},
        {"id": "field-6", "type": "Slider", "label": "Backdrop", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-7", "type": "Choices", "label": "Scale", "desktopColumns": 3, "initial": "Display", "options": ["Display", "H1", "H2"]},
        {"id": "field-8", "type": "Slider", "label": "Tracking", "initial": 12.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-9", "type": "Toggle", "label": "Auto fit width", "initial": true, "description": "캔버스 폭에 맞춘다"},
        {"id": "field-10", "type": "Segmented", "label": "Bubble", "desktopColumns": 3, "initial": "Round", "options": ["Round", "Sharp", "Cloud"]},
        {"id": "field-11", "type": "Segmented", "label": "Pointer", "desktopColumns": 3, "initial": "Left", "options": ["Left", "Bottom", "Right"]},
        {"id": "field-12", "type": "Slider", "label": "Padding", "initial": 20.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-13", "type": "Segmented", "label": "Footer · Content", "desktopColumns": 3, "initial": "Custom", "options": ["Custom", "Page no.", "Metadata"]},
        {"id": "field-14", "type": "Slider", "label": "Bottom inset", "initial": 32.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-15", "type": "Toggle", "label": "Repeat on pages", "initial": true, "description": "모든 아트보드에 반복한다"},
    ]},
    {"key": "camera-photo", "title": "Camera / Photo", "number": 3, "figmaNode": "196:3703", "selector": {"id": "selector", "type": "Choices", "label": "Source", "options": ["Camera", "Photo Library", "Document Scan", "RAW Capture"], "desktopColumns": 2, "initial": "Camera"}, "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Segmented", "label": "Lens", "desktopColumns": 3, "initial": "0.5×", "options": ["0.5×", "1×", "3×"]},
        {"id": "field-1", "type": "Slider", "label": "Exposure", "initial": 0.0, "minimum": -5, "maximum": 5, "step": 0.01, "unit": "EV", "decimals": 1},
        {"id": "field-2", "type": "Toggle", "label": "AE/AF lock", "initial": true, "description": "노출과 초점을 고정한다"},
        {"id": "field-3", "type": "Action", "label": "Capture photo", "initial": ""},
        {"id": "field-4", "type": "Choices", "label": "Source", "desktopColumns": 3, "initial": "Recents", "options": ["Recents", "Favorites", "RAW"]},
        {"id": "field-5", "type": "Segmented", "label": "Sort", "desktopColumns": 3, "initial": "Newest", "options": ["Newest", "Oldest", "Name"]},
        {"id": "field-6", "type": "Toggle", "label": "Include metadata", "initial": true, "description": "EXIF와 위치를 유지한다"},
        {"id": "field-7", "type": "Action", "label": "Place selected", "initial": ""},
        {"id": "field-8", "type": "Toggle", "label": "Auto capture", "initial": true, "description": "안정된 프레임을 자동 촬영한다"},
        {"id": "field-9", "type": "Segmented", "label": "Color", "desktopColumns": 3, "initial": "Color", "options": ["Color", "Gray", "B&W"]},
        {"id": "field-10", "type": "Slider", "label": "Scan contrast", "initial": 68.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-11", "type": "Action", "label": "Scan page", "initial": ""},
        {"id": "field-12", "type": "Segmented", "label": "Format", "desktopColumns": 3, "initial": "DNG", "options": ["DNG", "ProRAW", "RAW+"]},
        {"id": "field-13", "type": "Choices", "label": "Bit depth", "desktopColumns": 3, "initial": "12-bit", "options": ["12-bit", "14-bit", "16-bit"]},
        {"id": "field-14", "type": "Toggle", "label": "Long exposure NR", "initial": true, "description": "장노출 노이즈를 감소한다"},
        {"id": "field-15", "type": "Action", "label": "Capture RAW", "initial": ""},
    ]},
    {"key": "asset", "title": "Asset", "number": 4, "figmaNode": "196:3911", "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Field", "label": "Search", "initial": ""},
        {"id": "field-1", "type": "Choices", "label": "Type", "desktopColumns": 4, "initial": "All", "options": ["All", "Image", "Vector", "Audio"]},
        {"id": "field-2", "type": "Segmented", "label": "Library Browser · License", "desktopColumns": 3, "initial": "Owned", "options": ["Owned", "Free", "Premium"]},
        {"id": "field-3", "type": "Visual", "label": "Asset results", "initial": ""},
        {"id": "field-4", "type": "Field", "label": "Collection", "initial": "Campaign 2026"},
        {"id": "field-5", "type": "Segmented", "label": "Sort", "desktopColumns": 3, "initial": "Manual", "options": ["Manual", "Recent", "Name"]},
        {"id": "field-6", "type": "Toggle", "label": "Shared collection", "initial": true, "description": "팀과 동기화한다"},
        {"id": "field-7", "type": "Action", "label": "Create collection", "initial": ""},
        {"id": "field-8", "type": "Field", "label": "Source", "initial": ""},
        {"id": "field-9", "type": "Toggle", "label": "Auto update", "initial": true, "description": "최신 버전을 반영한다"},
        {"id": "field-10", "type": "Segmented", "label": "On conflict", "desktopColumns": 3, "initial": "Ask", "options": ["Ask", "Keep local", "Replace"]},
        {"id": "field-11", "type": "Action", "label": "Relink source", "initial": ""},
        {"id": "field-12", "type": "Segmented", "label": "Version", "desktopColumns": 3, "initial": "Latest", "options": ["Latest", "v12", "v11"]},
        {"id": "field-13", "type": "Field", "label": "License & Version · License", "desktopStacked": true, "initial": "Commercial / Global"},
        {"id": "field-14", "type": "Toggle", "label": "Pin version", "initial": true, "description": "현재 버전을 고정한다"},
        {"id": "field-15", "type": "Action", "label": "Inspect local files", "initial": ""},
    ]},
    {"key": "file", "title": "File", "number": 5, "figmaNode": "197:3529", "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Field", "label": "File location", "initial": "iCloud Drive / Dreamscapes"},
        {"id": "field-1", "type": "Choices", "label": "Format", "desktopColumns": 4, "initial": "IISC", "options": ["IISC", "PNG", "JPEG", "TIFF"]},
        {"id": "field-2", "type": "Toggle", "label": "Open as copy", "initial": true, "description": "원본 연결을 끊는다"},
        {"id": "field-3", "type": "Action", "label": "Choose file…", "initial": ""},
        {"id": "field-4", "type": "Segmented", "label": "Placement", "desktopColumns": 3, "initial": "Fit", "options": ["Fit", "Fill", "1:1"]},
        {"id": "field-5", "type": "Choices", "label": "Mode", "desktopColumns": 3, "initial": "Embed", "options": ["Embed", "Link", "Smart"]},
        {"id": "field-6", "type": "Action", "label": "Place into canvas", "initial": ""},
        {"id": "field-7", "type": "Toggle", "label": "Watch changes", "initial": true, "description": "외부 수정을 감지한다"},
        {"id": "field-8", "type": "Segmented", "label": "Missing file", "desktopColumns": 3, "initial": "Locate", "options": ["Locate", "Ignore", "Embed"]},
        {"id": "field-9", "type": "Action", "label": "Update now", "initial": ""},
        {"id": "field-10", "type": "Field", "label": "Name", "initial": "dreamscape-final"},
        {"id": "field-11", "type": "Toggle", "label": "Preserve layers", "initial": true, "description": "편집 구조를 유지한다"},
        {"id": "field-12", "type": "Action", "label": "Save document", "initial": ""},
    ]},
    {"key": "background", "title": "Background", "number": 6, "figmaNode": "197:3649", "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Choices", "label": "Fill", "desktopColumns": 3, "initial": "Solid", "options": ["Solid", "Linear", "Radial"]},
        {"id": "field-1", "type": "Field", "label": "Hex", "initial": "#12131A"},
        {"id": "field-2", "type": "Slider", "label": "Opacity", "initial": 100.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-3", "type": "Toggle", "label": "Extend to bleed", "initial": true, "description": "도련 영역까지 채운다"},
        {"id": "field-4", "type": "Segmented", "label": "Quality", "desktopColumns": 3, "initial": "Fast", "options": ["Fast", "Balanced", "Precise"]},
        {"id": "field-5", "type": "Slider", "label": "Edge refinement", "initial": 42.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-6", "type": "Toggle", "label": "Decontaminate color", "initial": true, "description": "가장자리 색 번짐을 제거한다"},
        {"id": "field-7", "type": "Action", "label": "Remove background", "initial": ""},
        {"id": "field-8", "type": "Visual", "label": "Detected regions", "initial": ""},
        {"id": "field-9", "type": "Slider", "label": "Confidence", "initial": 76.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-10", "type": "Choices", "label": "Target", "desktopColumns": 3, "initial": "Background", "options": ["Background", "Sky", "Ground"]},
        {"id": "field-11", "type": "Action", "label": "Create masks", "initial": ""},
        {"id": "field-12", "type": "Field", "label": "Prompt", "initial": "Soft studio, warm daylight"},
        {"id": "field-13", "type": "Choices", "label": "Source", "desktopColumns": 3, "initial": "Generate", "options": ["Generate", "Asset", "Photo"]},
        {"id": "field-14", "type": "Slider", "label": "Subject match", "initial": 84.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-15", "type": "Action", "label": "Generate replacement", "initial": ""},
    ]},
    {"key": "audio-track", "title": "Audio track", "number": 7, "figmaNode": "197:3907", "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Field", "label": "File", "initial": "music-bed.wav"},
        {"id": "field-1", "type": "Choices", "label": "Channels", "desktopColumns": 3, "initial": "Stereo", "options": ["Stereo", "Mono", "Split"]},
        {"id": "field-2", "type": "Segmented", "label": "Sample rate", "desktopColumns": 3, "initial": "44.1k", "options": ["44.1k", "48k", "96k"]},
        {"id": "field-3", "type": "Action", "label": "Import track", "initial": ""},
        {"id": "field-4", "type": "Visual", "label": "Waveform / selection", "initial": ""},
        {"id": "field-5", "type": "Field", "label": "Range", "initial": "Auto"},
        {"id": "field-6", "type": "Slider", "label": "Fade in", "initial": 0.8, "minimum": 0, "maximum": 60, "step": 0.1, "unit": "s", "decimals": 1},
        {"id": "field-7", "type": "Slider", "label": "Fade out", "initial": 1.2, "minimum": 0, "maximum": 60, "step": 0.1, "unit": "s", "decimals": 1},
        {"id": "field-8", "type": "Slider", "label": "Volume", "initial": -3.0, "minimum": -60, "maximum": 12, "step": 0.1, "unit": "dB", "decimals": 1},
        {"id": "field-9", "type": "Slider", "label": "Pan", "initial": 0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-10", "type": "Toggle", "label": "Auto ducking", "initial": true, "description": "다른 트랙의 활성 구간에서 감쇠한다"},
        {"id": "field-11", "type": "Slider", "label": "Ducking amount", "initial": -12.0, "minimum": -60, "maximum": 12, "step": 0.1, "unit": "dB", "decimals": 0},
        {"id": "field-12", "type": "Segmented", "label": "Sync to", "desktopColumns": 3, "initial": "Timeline", "options": ["Timeline", "Beat"]},
        {"id": "field-13", "type": "Slider", "label": "Offset", "desktopPositiveSign": true, "initial": 120.0, "minimum": -1000, "maximum": 1000, "step": 1, "unit": "ms", "decimals": 0},
        {"id": "field-14", "type": "Toggle", "label": "Stretch to fit", "initial": true, "description": "길이에 맞춰 시간 신축한다"},
        {"id": "field-15", "type": "Action", "label": "Analyze & sync", "initial": ""},
    ]},
    {"key": "canvas", "title": "Canvas", "number": 8, "figmaNode": "197:4160", "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Dimensions", "label": "Dimensions", "initial": [2048, 2732]},
        {"id": "field-1", "type": "Field", "label": "Resolution", "initial": "300 ppi"},
        {"id": "field-2", "type": "Toggle", "label": "Resample", "initial": true, "description": "픽셀 수를 함께 변경한다"},
        {"id": "field-3", "type": "Segmented", "label": "Anchor", "desktopColumns": 3, "initial": "Center", "options": ["Center", "Top-left", "Bottom-right"]},
        {"id": "field-4", "type": "Choices", "label": "Profile", "desktopColumns": 3, "initial": "Display P3", "options": ["Display P3", "sRGB", "Adobe RGB"]},
        {"id": "field-5", "type": "Segmented", "label": "Intent", "desktopColumns": 3, "initial": "Relative", "options": ["Relative", "Perceptual", "Absolute"]},
        {"id": "field-6", "type": "Toggle", "label": "Black point compensation", "initial": true, "description": "암부 손실을 보정한다"},
        {"id": "field-7", "type": "Action", "label": "Convert profile", "initial": ""},
        {"id": "field-8", "type": "Segmented", "label": "Angle", "desktopColumns": 3, "initial": "90° L", "options": ["90° L", "180°", "90° R"]},
        {"id": "field-9", "type": "Slider", "label": "Custom angle", "initial": 0.0, "minimum": -180, "maximum": 180, "step": 0.1, "unit": "°", "decimals": 1},
        {"id": "field-10", "type": "Toggle", "label": "Rotate layers", "initial": true, "description": "모든 레이어를 함께 회전한다"},
        {"id": "field-11", "type": "Action", "label": "Apply rotation", "initial": ""},
        {"id": "field-12", "type": "Segmented", "label": "Axis", "desktopColumns": 2, "initial": "Horizontal", "options": ["Horizontal", "Vertical"]},
        {"id": "field-13", "type": "Toggle", "label": "Mirror canvas only", "initial": true, "description": "콘텐츠는 유지한다"},
        {"id": "field-14", "type": "Toggle", "label": "Keep text readable", "initial": true, "description": "텍스트 방향을 복원한다"},
        {"id": "field-15", "type": "Action", "label": "Mirror canvas", "initial": ""},
        {"id": "field-16", "type": "Segmented", "label": "Overlay", "desktopColumns": 3, "initial": "Crop", "options": ["Crop", "Bleed", "Safe"]},
        {"id": "field-17", "type": "Field", "label": "Bleed", "initial": "3 mm"},
        {"id": "field-18", "type": "Field", "label": "Safe inset", "initial": "5%"},
        {"id": "field-19", "type": "Toggle", "label": "Delete cropped pixels", "initial": true, "description": "비파괴 크롭을 유지한다"},
    ]},
    {"key": "generative", "title": "Generative", "number": 9, "figmaNode": "197:4366", "selector": {"id": "selector", "type": "Choices", "label": "Mode", "options": ["Text-to-image", "Image-to-image", "Inpaint", "Outpaint"], "desktopColumns": 2, "initial": "Text-to-image"}, "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Field", "label": "Prompt", "initial": "Cinematic glass garden at dusk"},
        {"id": "field-1", "type": "Field", "label": "Negative", "initial": "text, watermark, artifacts"},
        {"id": "field-2", "type": "Choices", "label": "Model", "desktopColumns": 3, "initial": "Standard", "options": ["Standard", "Artwork", "Photo"]},
        {"id": "field-3", "type": "Action", "label": "Generate 4 variations", "initial": ""},
        {"id": "field-4", "type": "Field", "label": "Reference", "initial": "Current layer"},
        {"id": "field-5", "type": "Slider", "label": "Denoise strength", "initial": 58.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-6", "type": "Slider", "label": "Structure lock", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-7", "type": "Action", "label": "Generate from image", "initial": ""},
        {"id": "field-8", "type": "Visual", "label": "Inpaint mask", "initial": ""},
        {"id": "field-9", "type": "Slider", "label": "Mask feather", "initial": 18.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-10", "type": "Action", "label": "Inpaint selection", "initial": ""},
        {"id": "field-11", "type": "Segmented", "label": "Direction", "desktopColumns": 3, "initial": "All", "options": ["All", "Left/Right", "Top/Bottom"]},
        {"id": "field-12", "type": "Field", "label": "Expansion", "initial": "512 px"},
        {"id": "field-13", "type": "Toggle", "label": "Match perspective", "initial": true, "description": "기존 소실점을 유지한다"},
        {"id": "field-14", "type": "Action", "label": "Expand canvas", "initial": ""},
        {"id": "field-15", "type": "Choices", "label": "Control", "desktopColumns": 4, "initial": "Canny", "options": ["Canny", "Depth", "Pose", "Lineart"]},
        {"id": "field-16", "type": "Slider", "label": "Weight", "initial": 0.85, "minimum": 0, "maximum": 4, "step": 0.01, "unit": "", "decimals": 2},
        {"id": "field-17", "type": "Field", "label": "Guidance range", "initial": "0.05 — 0.90"},
        {"id": "field-18", "type": "Toggle", "label": "Pixel perfect", "initial": true, "description": "해상도에 자동 맞춘다"},
        {"id": "field-19", "type": "Choices", "label": "Mode", "desktopColumns": 3, "initial": "Image", "options": ["Image", "Face", "Style"]},
        {"id": "field-20", "type": "Slider", "label": "Reference weight", "initial": 0.7, "minimum": 0, "maximum": 4, "step": 0.01, "unit": "", "decimals": 2},
        {"id": "field-21", "type": "Slider", "label": "Style fidelity", "initial": 0.55, "minimum": 0, "maximum": 4, "step": 0.01, "unit": "", "decimals": 2},
        {"id": "field-22", "type": "Action", "label": "Add reference", "initial": ""},
        {"id": "field-23", "type": "Field", "label": "LoRA file", "initial": ""},
        {"id": "field-24", "type": "Slider", "label": "LoRA weight", "initial": 0.82, "minimum": 0, "maximum": 4, "step": 0.01, "unit": "", "decimals": 2},
        {"id": "field-25", "type": "Field", "label": "VAE", "initial": "Auto / model default"},
        {"id": "field-26", "type": "Toggle", "label": "Bake into result", "initial": true, "description": "결과 레이어에 고정한다"},
        {"id": "field-27", "type": "Segmented", "label": "Scale", "desktopColumns": 3, "initial": "2×", "options": ["2×", "4×", "8×"]},
        {"id": "field-28", "type": "Choices", "label": "Resampling", "desktopColumns": 2, "initial": "Smooth", "options": ["Nearest", "Smooth"]},
        {"id": "field-29", "type": "Slider", "label": "Refiner strength", "initial": 32.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-30", "type": "Action", "label": "Upscale layer", "initial": ""},
    ]},
    {"key": "layers", "title": "Layers", "number": 10, "figmaNode": "197:4816", "selector": {"id": "selector", "type": "Choices", "label": "Layer type", "options": ["Generated", "Dynamic", "Pixel", "Vector", "Adjustment", "Group", "Mask"], "desktopColumns": 4, "initial": "Generated"}, "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Visual", "label": "Generation lineage", "initial": ""},
        {"id": "field-1", "type": "Field", "label": "Seed", "initial": "Unknown"},
        {"id": "field-2", "type": "Toggle", "label": "Regenerate non-destructively", "initial": true, "description": "새 버전을 스택에 추가한다"},
        {"id": "field-3", "type": "Action", "label": "Generate variation", "initial": ""},
        {"id": "field-4", "type": "Field", "label": "Source", "initial": "Linked photo / hero"},
        {"id": "field-5", "type": "Toggle", "label": "Live effects", "initial": true, "description": "파라미터 변경을 즉시 반영한다"},
        {"id": "field-6", "type": "Segmented", "label": "Cache", "desktopColumns": 3, "initial": "Auto", "options": ["Auto", "Full", "Off"]},
        {"id": "field-7", "type": "Action", "label": "Convert to static", "initial": ""},
        {"id": "field-8", "type": "Segmented", "label": "Lock", "desktopColumns": 3, "initial": "None", "options": ["None", "Pixels", "Position"]},
        {"id": "field-9", "type": "Toggle", "label": "Preserve alpha", "initial": true, "description": "투명 픽셀을 보호한다"},
        {"id": "field-10", "type": "Slider", "label": "Opacity", "initial": 100.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-11", "type": "Action", "label": "Raster operations", "initial": ""},
        {"id": "field-12", "type": "Visual", "label": "Path hierarchy", "initial": ""},
        {"id": "field-13", "type": "Segmented", "label": "Boolean", "desktopColumns": 3, "initial": "Union", "options": ["Union", "Subtract", "Intersect"]},
        {"id": "field-14", "type": "Toggle", "label": "Scale strokes", "initial": true, "description": "변형 시 선 두께를 비례한다"},
        {"id": "field-15", "type": "Action", "label": "Edit paths", "initial": ""},
        {"id": "field-16", "type": "Choices", "label": "Adjustment", "desktopColumns": 3, "initial": "Curves", "options": ["Curves", "HSL", "Exposure"]},
        {"id": "field-17", "type": "Segmented", "label": "Scope", "desktopColumns": 3, "initial": "Below", "options": ["Below", "Clipped", "Group"]},
        {"id": "field-18", "type": "Action", "label": "Apply adjustment", "initial": ""},
        {"id": "field-19", "type": "Field", "label": "Group name", "initial": "Hero composite"},
        {"id": "field-20", "type": "Segmented", "label": "Blend", "desktopColumns": 3, "initial": "Normal", "options": ["Normal", "Multiply", "Screen"]},
        {"id": "field-21", "type": "Choices", "label": "Label", "desktopColumns": 3, "initial": "Violet", "options": ["Violet", "Mint", "Amber"]},
        {"id": "field-22", "type": "Toggle", "label": "Collapse on close", "initial": true, "description": "문서 재개 시 접는다"},
        {"id": "field-23", "type": "Visual", "label": "Mask preview", "initial": ""},
        {"id": "field-24", "type": "Segmented", "label": "Type", "desktopColumns": 3, "initial": "Pixel", "options": ["Pixel"]},
        {"id": "field-25", "type": "Slider", "label": "Density", "initial": 100.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-26", "type": "Slider", "label": "Feather", "initial": 12.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
    ]},
    {"key": "select", "title": "Select", "number": 11, "figmaNode": "197:5165", "selector": {"id": "selector", "type": "Choices", "label": "Shape", "options": ["Lasso", "Rectangle", "Triangle", "Magic Wand", "Object"], "desktopColumns": 4, "initial": "Lasso"}, "desktopActionColumns": 1, "fields": [
        {"id": "field-0", "type": "Segmented", "label": "Mode", "desktopColumns": 3, "initial": "New", "options": ["New", "Add", "Subtract"]},
        {"id": "field-1", "type": "Slider", "label": "Smoothing", "initial": 36.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-2", "type": "Slider", "label": "Feather", "initial": 8.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-3", "type": "Toggle", "label": "Close automatically", "initial": true, "description": "포인터 해제 시 경로를 닫는다"},
        {"id": "field-4", "type": "Choices", "label": "Ratio", "desktopColumns": 3, "initial": "Free", "options": ["Free", "1:1", "4:5"]},
        {"id": "field-5", "type": "Toggle", "label": "From center", "initial": true, "description": "중심점에서 확장한다"},
        {"id": "field-6", "type": "Slider", "label": "Rotation", "initial": 0.0, "minimum": -180, "maximum": 180, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-7", "type": "Toggle", "label": "Snap vertices", "initial": true, "description": "가이드와 개체에 스냅한다"},
        {"id": "field-8", "type": "Slider", "label": "Corner roundness", "initial": 0.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-9", "type": "Slider", "label": "Tolerance", "initial": 32.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-10", "type": "Toggle", "label": "Contiguous", "initial": true, "description": "연결된 영역만 선택한다"},
        {"id": "field-11", "type": "Toggle", "label": "Sample all layers", "initial": true, "description": "합성 결과를 표본화한다"},
        {"id": "field-12", "type": "Slider", "label": "Edge refine", "initial": 18.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-13", "type": "Choices", "label": "Target", "desktopColumns": 3, "initial": "Object", "options": ["Object", "Person", "Part"]},
        {"id": "field-14", "type": "Visual", "label": "Detected objects", "initial": ""},
        {"id": "field-15", "type": "Slider", "label": "Confidence", "initial": 82.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-16", "type": "Action", "label": "Select detected", "initial": ""},
    ]},
    {"key": "color", "title": "Color", "number": 12, "figmaNode": "197:5507", "desktopActionColumns": 1, "fields": [
        {"id": "field-0", "type": "Visual", "label": "Luminance histogram", "initial": ""},
        {"id": "field-1", "type": "Slider", "label": "Exposure", "desktopPositiveSign": true, "initial": 0.35, "minimum": -5, "maximum": 5, "step": 0.01, "unit": "EV", "decimals": 2},
        {"id": "field-2", "type": "Slider", "label": "Offset", "initial": 0.0, "minimum": -1, "maximum": 1, "step": 0.01, "unit": "", "decimals": 2},
        {"id": "field-3", "type": "Toggle", "label": "Protect highlights", "initial": true, "description": "밝은 영역 클리핑을 억제한다"},
        {"id": "field-4", "type": "Slider", "label": "Contrast", "desktopPositiveSign": true, "initial": 18.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-5", "type": "Slider", "label": "Pivot", "initial": 50.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-6", "type": "Toggle", "label": "Linear light", "initial": true, "description": "선형 공간에서 계산한다"},
        {"id": "field-7", "type": "Visual", "label": "Contrast response", "initial": ""},
        {"id": "field-8", "type": "Slider", "label": "Highlights", "initial": -24.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-9", "type": "Slider", "label": "Highlights · Range", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-10", "type": "Slider", "label": "Rolloff", "initial": 28.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-11", "type": "Toggle", "label": "Preserve color", "initial": true, "description": "채도 변화를 억제한다"},
        {"id": "field-12", "type": "Slider", "label": "Shadows", "desktopPositiveSign": true, "initial": 32.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-13", "type": "Slider", "label": "Shadows · Range", "initial": 38.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-14", "type": "Slider", "label": "Black protect", "initial": 16.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-15", "type": "Toggle", "label": "Adaptive", "initial": true, "description": "지역 대비를 고려한다"},
        {"id": "field-16", "type": "Slider", "label": "Whites", "desktopPositiveSign": true, "initial": 12.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-17", "type": "Slider", "label": "Whites · Clip threshold", "initial": 98.5, "minimum": 0, "maximum": 100, "step": 0.1, "unit": "%", "decimals": 1},
        {"id": "field-18", "type": "Toggle", "label": "Show clipping", "initial": true, "description": "클리핑 픽셀을 경고한다"},
        {"id": "field-19", "type": "Visual", "label": "Upper tone range", "initial": ""},
        {"id": "field-20", "type": "Slider", "label": "Blacks", "initial": -8.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-21", "type": "Slider", "label": "Blacks · Clip threshold", "initial": 1.2, "minimum": 0, "maximum": 100, "step": 0.1, "unit": "%", "decimals": 1},
        {"id": "field-22", "type": "Toggle", "label": "Lift blacks", "initial": true, "description": "매트한 암부를 만든다"},
        {"id": "field-23", "type": "Visual", "label": "Lower tone range", "initial": ""},
        {"id": "field-24", "type": "Segmented", "label": "Unit", "desktopColumns": 2, "initial": "Kelvin", "options": ["Kelvin", "Relative"]},
        {"id": "field-25", "type": "Slider", "label": "Temperature", "initial": 5600.0, "minimum": 1000, "maximum": 20000, "step": 50, "unit": "K", "decimals": 0},
        {"id": "field-26", "type": "Choices", "label": "Illuminant", "desktopColumns": 3, "initial": "Daylight", "options": ["Daylight", "Tungsten", "Shade"]},
        {"id": "field-27", "type": "Toggle", "label": "Neutralize picker", "initial": true, "description": "회색점을 직접 지정한다"},
        {"id": "field-28", "type": "Slider", "label": "Tint", "desktopPositiveSign": true, "initial": 6.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-29", "type": "Visual", "label": "Tint spectrum", "initial": ""},
        {"id": "field-30", "type": "Toggle", "label": "Tint · Skin protection", "initial": true, "description": "피부색 이동을 완화한다"},
        {"id": "field-31", "type": "Action", "label": "Pick neutral point", "initial": ""},
        {"id": "field-32", "type": "Slider", "label": "Vibrance", "desktopPositiveSign": true, "initial": 22.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-33", "type": "Slider", "label": "Vibrance · Skin protection", "initial": 68.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-34", "type": "Slider", "label": "Low-sat bias", "initial": 74.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-35", "type": "Toggle", "label": "Gamut limit", "initial": true, "description": "색역 이탈을 방지한다"},
        {"id": "field-36", "type": "Slider", "label": "Master saturation", "desktopPositiveSign": true, "initial": 8.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-37", "type": "Choices", "label": "Saturation · Channel", "desktopColumns": 3, "initial": "Master", "options": ["Master", "Red", "Blue"]},
        {"id": "field-38", "type": "Slider", "label": "Channel amount", "initial": 0.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-39", "type": "Toggle", "label": "Colorize", "initial": true, "description": "단일 색조로 변환한다"},
        {"id": "field-40", "type": "Visual", "label": "Three-way wheels", "initial": ""},
        {"id": "field-41", "type": "Segmented", "label": "Grading · Range", "desktopColumns": 3, "initial": "Shadows", "options": ["Shadows", "Midtones", "Highlights"]},
        {"id": "field-42", "type": "Slider", "label": "Hue", "initial": 214.0, "minimum": 0, "maximum": 360, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-43", "type": "Slider", "label": "Balance", "desktopPositiveSign": true, "initial": 8.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-44", "type": "Visual", "label": "Tone curve", "initial": ""},
        {"id": "field-45", "type": "Choices", "label": "Curve · Channel", "desktopColumns": 4, "initial": "RGB", "options": ["RGB", "Red", "Green", "Blue"]},
        {"id": "field-46", "type": "Segmented", "label": "Interpolation", "desktopColumns": 2, "initial": "Smooth", "options": ["Smooth", "Linear"]},
        {"id": "field-47", "type": "Toggle", "label": "Histogram overlay", "initial": true, "description": "분포를 곡선 뒤에 표시한다"},
    ]},
    {"key": "effects", "title": "Effects", "number": 13, "figmaNode": "197:6429", "fields": [
        {"id": "field-0", "type": "Visual", "label": "Filter presets", "initial": ""},
        {"id": "field-1", "type": "Choices", "label": "Category", "desktopColumns": 3, "initial": "Featured", "options": ["Featured", "Film", "B&W"]},
        {"id": "field-2", "type": "Slider", "label": "Filter · Amount", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-3", "type": "Toggle", "label": "Preserve skin", "initial": true, "description": "인물 피부색을 보호한다"},
        {"id": "field-4", "type": "Choices", "label": "Blur · Type", "desktopColumns": 3, "initial": "Gaussian", "options": ["Gaussian", "Motion"]},
        {"id": "field-5", "type": "Slider", "label": "Blur · Radius", "initial": 18.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-6", "type": "Slider", "label": "Angle", "initial": 0.0, "minimum": -180, "maximum": 180, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-7", "type": "Toggle", "label": "Edge-aware", "initial": true, "description": "개체 경계를 보존한다"},
        {"id": "field-8", "type": "Slider", "label": "Texture", "desktopPositiveSign": true, "initial": 16.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-9", "type": "Slider", "label": "Texture · Scale", "initial": 42.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-10", "type": "Segmented", "label": "Target", "desktopColumns": 3, "initial": "All", "options": ["All", "Skin", "Background"]},
        {"id": "field-11", "type": "Toggle", "label": "Protect edges", "initial": true, "description": "선명한 윤곽을 유지한다"},
        {"id": "field-12", "type": "Slider", "label": "Clarity", "desktopPositiveSign": true, "initial": 24.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-13", "type": "Slider", "label": "Clarity · Radius", "initial": 36.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-14", "type": "Toggle", "label": "Natural mode", "initial": true, "description": "헤일로를 억제한다"},
        {"id": "field-15", "type": "Slider", "label": "Edge mask", "initial": 44.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-16", "type": "Slider", "label": "Dehaze", "desktopPositiveSign": true, "initial": 28.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-17", "type": "Slider", "label": "Depth bias", "initial": 58.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-18", "type": "Toggle", "label": "Auto color restore", "initial": true, "description": "제거 후 색을 복구한다"},
        {"id": "field-19", "type": "Visual", "label": "Atmospheric range", "initial": ""},
        {"id": "field-20", "type": "Slider", "label": "Vignette · Amount", "initial": -22.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-21", "type": "Slider", "label": "Midpoint", "initial": 46.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-22", "type": "Slider", "label": "Roundness", "desktopPositiveSign": true, "initial": 18.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-23", "type": "Slider", "label": "Feather", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-24", "type": "Slider", "label": "Grain · Amount", "initial": 24.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-25", "type": "Slider", "label": "Size", "initial": 32.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-26", "type": "Slider", "label": "Roughness", "initial": 58.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-27", "type": "Choices", "label": "Response", "desktopColumns": 3, "initial": "Film", "options": ["Film", "Digital", "Mono"]},
        {"id": "field-28", "type": "Slider", "label": "Sharpening · Amount", "initial": 64.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-29", "type": "Slider", "label": "Sharpening · Radius", "initial": 1.0, "minimum": 0, "maximum": 256, "step": 0.1, "unit": "px", "decimals": 1},
        {"id": "field-30", "type": "Slider", "label": "Sharpening · Detail", "initial": 28.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-31", "type": "Slider", "label": "Masking", "initial": 42.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-32", "type": "Choices", "label": "Noise · Type", "desktopColumns": 3, "initial": "Uniform", "options": ["Uniform", "Film"]},
        {"id": "field-33", "type": "Slider", "label": "Noise · Amount", "initial": 12.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-34", "type": "Slider", "label": "Noise · Scale", "initial": 1.0, "minimum": 0, "maximum": 4, "step": 0.1, "unit": "", "decimals": 1},
        {"id": "field-35", "type": "Toggle", "label": "Monochromatic", "initial": true, "description": "색상 노이즈를 유지한다"},
        {"id": "field-36", "type": "Slider", "label": "Luminance", "initial": 28.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-37", "type": "Slider", "label": "Luminance NR · Detail", "initial": 54.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-38", "type": "Slider", "label": "Contrast", "initial": 12.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-39", "type": "Visual", "label": "Noise frequency", "initial": ""},
        {"id": "field-40", "type": "Slider", "label": "Color", "initial": 32.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-41", "type": "Slider", "label": "Color NR · Detail", "initial": 48.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-42", "type": "Slider", "label": "Smoothness", "initial": 56.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-43", "type": "Toggle", "label": "Remove color speckles", "initial": true, "description": "고립 색상점을 제거한다"},
        {"id": "field-44", "type": "Field", "label": "Profile", "initial": "Auto / 24 mm F1.8"},
        {"id": "field-45", "type": "Toggle", "label": "Distortion", "initial": true, "description": "배럴·핀쿠션을 보정한다"},
        {"id": "field-46", "type": "Toggle", "label": "Chromatic aberration", "initial": true, "description": "색수차를 제거한다"},
        {"id": "field-47", "type": "Slider", "label": "Manual vignette", "desktopPositiveSign": true, "initial": 6.0, "minimum": -100, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
    ]},
    {"key": "retouch", "title": "Retouch", "number": 14, "figmaNode": "197:7454", "selector": {"id": "selector", "type": "Choices", "label": "Mode", "options": ["Remove", "Inpaint", "Heal", "Clone", "Content-aware", "Face Refine"], "desktopColumns": 3, "initial": "Remove"}, "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Slider", "label": "Brush size", "initial": 84.0, "minimum": 0, "maximum": 2048, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-1", "type": "Toggle", "label": "Detect object", "initial": true, "description": "스트로크 내부 개체를 자동 선택한다"},
        {"id": "field-2", "type": "Segmented", "label": "Fill", "desktopColumns": 3, "initial": "Auto", "options": ["Auto", "Content", "Generate"]},
        {"id": "field-3", "type": "Action", "label": "Remove marked object", "initial": ""},
        {"id": "field-4", "type": "Visual", "label": "Repair mask", "initial": ""},
        {"id": "field-5", "type": "Field", "label": "Prompt", "initial": "Clean wall texture"},
        {"id": "field-6", "type": "Slider", "label": "Context strength", "initial": 76.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-7", "type": "Action", "label": "Inpaint area", "initial": ""},
        {"id": "field-8", "type": "Slider", "label": "Feather", "initial": 62.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-9", "type": "Segmented", "label": "Source", "desktopColumns": 3, "initial": "Auto", "options": ["Auto", "Pick", "Pattern"]},
        {"id": "field-10", "type": "Toggle", "label": "Sample all layers", "initial": true, "description": "합성 결과에서 샘플링한다"},
        {"id": "field-11", "type": "Action", "label": "Set clone source", "initial": ""},
        {"id": "field-12", "type": "Slider", "label": "Scale", "initial": 100.0, "minimum": 0, "maximum": 200, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-13", "type": "Slider", "label": "Rotation", "initial": 0.0, "minimum": -180, "maximum": 180, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-14", "type": "Toggle", "label": "Aligned", "initial": true, "description": "스트로크 사이 오프셋을 유지한다"},
        {"id": "field-15", "type": "Visual", "label": "Sampling overlay", "initial": ""},
        {"id": "field-16", "type": "Segmented", "label": "Adaptation", "desktopColumns": 3, "initial": "Auto", "options": ["Auto", "Color", "Rotation"]},
        {"id": "field-17", "type": "Slider", "label": "Color adaptation", "initial": 68.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-18", "type": "Action", "label": "Apply repair", "initial": ""},
        {"id": "field-19", "type": "Choices", "label": "Region", "desktopColumns": 3, "initial": "Skin", "options": ["Skin", "Eyes", "Teeth"]},
        {"id": "field-20", "type": "Slider", "label": "Skin smoothing", "initial": 18.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-21", "type": "Slider", "label": "Eye detail", "initial": 12.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "", "decimals": 0},
        {"id": "field-22", "type": "Toggle", "label": "Preserve identity", "initial": true, "description": "얼굴 비율 변화를 제한한다"},
    ]},
    {"key": "fill", "title": "Fill", "number": 15, "figmaNode": "197:7857", "selector": {"id": "selector", "type": "Choices", "label": "Fill", "options": ["Solid", "Gradient", "Pattern", "Generative"], "desktopColumns": 4, "initial": "Solid"}, "desktopActionColumns": 1, "fields": [
        {"id": "field-0", "type": "Visual", "label": "Color palette", "initial": ""},
        {"id": "field-1", "type": "Field", "label": "Hex", "initial": "#8B7CFF"},
        {"id": "field-2", "type": "Slider", "label": "Opacity", "initial": 100.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-3", "type": "Segmented", "label": "Blend", "desktopColumns": 3, "initial": "Normal", "options": ["Normal", "Multiply", "Screen"]},
        {"id": "field-4", "type": "Segmented", "label": "Type", "desktopColumns": 3, "initial": "Linear", "options": ["Linear", "Radial", "Angular"]},
        {"id": "field-5", "type": "Visual", "label": "Gradient palette", "initial": ""},
        {"id": "field-6", "type": "Slider", "label": "Angle", "initial": 32.0, "minimum": -180, "maximum": 180, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-7", "type": "Toggle", "label": "Dither", "initial": true, "description": "밴딩을 감소한다"},
        {"id": "field-8", "type": "Visual", "label": "Pattern palette", "initial": ""},
        {"id": "field-9", "type": "Slider", "label": "Scale", "initial": 82.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-10", "type": "Slider", "label": "Rotation", "initial": 0.0, "minimum": -180, "maximum": 180, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-11", "type": "Toggle", "label": "Seamless", "initial": true, "description": "타일 경계를 맞춘다"},
        {"id": "field-12", "type": "Field", "label": "Prompt", "initial": "Iridescent glass petals"},
        {"id": "field-13", "type": "Slider", "label": "Variation", "initial": 64.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-14", "type": "Toggle", "label": "Match lighting", "initial": true, "description": "주변 광원을 추정한다"},
        {"id": "field-15", "type": "Action", "label": "Generate fill", "initial": ""},
    ]},
    {"key": "brush", "title": "Brush", "number": 16, "figmaNode": "197:8150", "desktopActionColumns": 1, "fields": [
        {"id": "field-0", "type": "Field", "label": "Search", "initial": ""},
        {"id": "field-1", "type": "Choices", "label": "Category", "desktopColumns": 3, "initial": "Paint", "options": ["Paint", "Ink", "Texture"]},
        {"id": "field-2", "type": "Visual", "label": "Brush presets", "initial": ""},
        {"id": "field-3", "type": "Action", "label": "Browse presets", "initial": ""},
        {"id": "field-4", "type": "Slider", "label": "Size", "initial": 96.0, "minimum": 0, "maximum": 2048, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-5", "type": "Slider", "label": "Hardness", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-6", "type": "Toggle", "label": "Relative to zoom", "initial": true, "description": "화면 배율에 크기를 고정한다"},
        {"id": "field-7", "type": "Choices", "label": "Shape", "desktopColumns": 3, "initial": "Round", "options": ["Round", "Square", "Diamond"]},
        {"id": "field-8", "type": "Slider", "label": "Opacity", "initial": 82.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-9", "type": "Slider", "label": "Flow", "initial": 36.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-10", "type": "Toggle", "label": "Build-up", "initial": true, "description": "중첩 스트로크를 누적한다"},
        {"id": "field-11", "type": "Segmented", "label": "Blend", "desktopColumns": 4, "initial": "Normal", "options": ["Normal", "Multiply", "Screen", "Overlay"]},
        {"id": "field-12", "type": "Slider", "label": "Pressure → Size", "initial": 84.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-13", "type": "Slider", "label": "Pressure → Opacity", "initial": 58.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-14", "type": "Slider", "label": "Tilt → Angle", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-15", "type": "Toggle", "label": "Invert pressure", "initial": true, "description": "입력 곡선을 반전한다"},
        {"id": "field-16", "type": "Slider", "label": "Spacing", "initial": 12.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-17", "type": "Slider", "label": "Smoothing", "initial": 46.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-18", "type": "Slider", "label": "Streamline", "initial": 34.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-19", "type": "Toggle", "label": "Catch-up", "initial": true, "description": "지연 후 포인터를 따라간다"},
        {"id": "field-20", "type": "Visual", "label": "Foreground / mix", "initial": ""},
        {"id": "field-21", "type": "Slider", "label": "Wet mix", "initial": 28.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-22", "type": "Slider", "label": "Color dynamics", "initial": 18.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-23", "type": "Segmented", "label": "Mode", "desktopColumns": 3, "initial": "Rope", "options": ["Rope", "Predictive", "Pulled"]},
        {"id": "field-24", "type": "Slider", "label": "Strength", "initial": 56.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-25", "type": "Slider", "label": "Delay", "initial": 24.0, "minimum": 0, "maximum": 1000, "step": 1, "unit": "ms", "decimals": 0},
        {"id": "field-26", "type": "Toggle", "label": "Angle snapping", "initial": true, "description": "설정 각도에 선을 고정한다"},
    ]},
    {"key": "auto-enhance", "title": "Auto enhance", "number": 17, "figmaNode": "197:8764", "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Visual", "label": "Before / after tone", "initial": ""},
        {"id": "field-1", "type": "Slider", "label": "Auto Tone · Strength", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-2", "type": "Toggle", "label": "Protect highlights", "initial": true, "description": "밝은 영역을 보존한다"},
        {"id": "field-3", "type": "Action", "label": "Analyze tone", "initial": ""},
        {"id": "field-4", "type": "Visual", "label": "Detected palette", "initial": ""},
        {"id": "field-5", "type": "Slider", "label": "Auto Color · Strength", "initial": 68.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-6", "type": "Toggle", "label": "Preserve skin", "initial": true, "description": "인물 피부색을 보호한다"},
        {"id": "field-7", "type": "Action", "label": "Analyze color", "initial": ""},
        {"id": "field-8", "type": "Slider", "label": "Detail recovery", "initial": 62.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-9", "type": "Slider", "label": "Noise balance", "initial": 44.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-10", "type": "Toggle", "label": "Avoid halos", "initial": true, "description": "과도한 윤곽을 억제한다"},
        {"id": "field-11", "type": "Action", "label": "Enhance detail", "initial": ""},
        {"id": "field-12", "type": "Visual", "label": "Detected subject", "initial": ""},
        {"id": "field-13", "type": "Choices", "label": "Target", "desktopColumns": 3, "initial": "Face", "options": ["Face", "Subject", "People"]},
        {"id": "field-14", "type": "Slider", "label": "Enhancement", "initial": 46.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-15", "type": "Toggle", "label": "Natural result", "initial": true, "description": "보정 상한을 제한한다"},
        {"id": "field-16", "type": "Toggle", "label": "Lens profile", "initial": true, "description": "촬영 정보를 사용한다"},
        {"id": "field-17", "type": "Toggle", "label": "Auto upright", "initial": true, "description": "수직선을 탐지한다"},
        {"id": "field-18", "type": "Slider", "label": "Rotation", "initial": 0.0, "minimum": -180, "maximum": 180, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-19", "type": "Action", "label": "Correct geometry", "initial": ""},
    ]},
    {"key": "masking", "title": "Masking", "number": 18, "figmaNode": "197:9090", "selector": {"id": "selector", "type": "Choices", "label": "Mask", "options": ["Brush", "Linear", "Radial", "Subject", "Region", "Range"], "desktopColumns": 4, "initial": "Brush"}, "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Slider", "label": "Brush size", "initial": 84.0, "minimum": 0, "maximum": 2048, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-1", "type": "Slider", "label": "Brush Mask · Feather", "initial": 58.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-2", "type": "Segmented", "label": "Mode", "desktopColumns": 3, "initial": "Paint", "options": ["Paint", "Erase", "Auto"]},
        {"id": "field-3", "type": "Toggle", "label": "Auto mask", "initial": true, "description": "색상·경계를 따라간다"},
        {"id": "field-4", "type": "Slider", "label": "Rotation", "initial": 0.0, "minimum": -180, "maximum": 180, "step": 1, "unit": "°", "decimals": 0},
        {"id": "field-5", "type": "Slider", "label": "Linear Gradient · Feather", "initial": 64.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-6", "type": "Toggle", "label": "Invert", "initial": true, "description": "영향 영역을 반전한다"},
        {"id": "field-7", "type": "Action", "label": "Draw gradient", "initial": ""},
        {"id": "field-8", "type": "Slider", "label": "Roundness", "initial": 72.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-9", "type": "Slider", "label": "Radial Gradient · Feather", "initial": 68.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-10", "type": "Visual", "label": "Detected subjects", "initial": ""},
        {"id": "field-11", "type": "Choices", "label": "Target", "desktopColumns": 3, "initial": "Main", "options": ["Main", "Person", "Object"]},
        {"id": "field-12", "type": "Slider", "label": "Edge refine", "initial": 34.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-13", "type": "Action", "label": "Create subject mask", "initial": ""},
        {"id": "field-14", "type": "Choices", "label": "Region", "desktopColumns": 3, "initial": "Sky", "options": ["Sky", "Background", "Depth"]},
        {"id": "field-15", "type": "Visual", "label": "Semantic depth", "initial": ""},
        {"id": "field-16", "type": "Slider", "label": "Depth range", "initial": [42, 78], "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-17", "type": "Toggle", "label": "Include reflections", "initial": true, "description": "반사 영역을 포함한다"},
        {"id": "field-18", "type": "Segmented", "label": "Range", "desktopColumns": 2, "initial": "Color", "options": ["Color", "Luminance"]},
        {"id": "field-19", "type": "Visual", "label": "Selected range", "initial": ""},
        {"id": "field-20", "type": "Slider", "label": "Range width", "initial": 28.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-21", "type": "Slider", "label": "Smoothness", "initial": 54.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-22", "type": "Segmented", "label": "Operation", "desktopColumns": 3, "initial": "Add", "options": ["Add", "Intersect", "Subtract"]},
        {"id": "field-23", "type": "Slider", "label": "Combine · Feather", "initial": 12.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-24", "type": "Slider", "label": "Contrast", "initial": 18.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-25", "type": "Toggle", "label": "Decontaminate edge", "initial": true, "description": "경계 색 번짐을 제거한다"},
    ]},
    {"key": "eraser", "title": "Eraser", "number": 19, "figmaNode": "197:9637", "selector": {"id": "selector", "type": "Choices", "label": "Eraser", "options": ["Pixel", "Object", "Vector", "Restore / Protect"], "desktopColumns": 2, "initial": "Pixel"}, "desktopActionColumns": 2, "fields": [
        {"id": "field-0", "type": "Slider", "label": "Size", "initial": 72.0, "minimum": 0, "maximum": 2048, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-1", "type": "Slider", "label": "Hardness", "initial": 64.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-2", "type": "Slider", "label": "Opacity", "initial": 100.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-3", "type": "Toggle", "label": "Erase to history", "initial": true, "description": "복원 가능한 기록으로 남긴다"},
        {"id": "field-4", "type": "Visual", "label": "Detected object", "initial": ""},
        {"id": "field-5", "type": "Slider", "label": "Boundary expand", "initial": 8.0, "minimum": 0, "maximum": 256, "step": 1, "unit": "px", "decimals": 0},
        {"id": "field-6", "type": "Segmented", "label": "Fill", "desktopColumns": 3, "initial": "Content", "options": ["Content", "Generate", "Transparent"]},
        {"id": "field-7", "type": "Action", "label": "Erase object", "initial": ""},
        {"id": "field-8", "type": "Segmented", "label": "Vector Eraser · Mode", "desktopColumns": 3, "initial": "Split path", "options": ["Split path", "Erase stroke", "Trim"]},
        {"id": "field-9", "type": "Toggle", "label": "Keep closed paths", "initial": true, "description": "도형 폐합을 유지한다"},
        {"id": "field-10", "type": "Action", "label": "Apply vector erase", "initial": ""},
        {"id": "field-11", "type": "Segmented", "label": "Restore · Mode", "desktopColumns": 2, "initial": "Restore", "options": ["Restore", "Protect"]},
        {"id": "field-12", "type": "Slider", "label": "Feather", "initial": 52.0, "minimum": 0, "maximum": 100, "step": 1, "unit": "%", "decimals": 0},
        {"id": "field-13", "type": "Toggle", "label": "Show erased pixels", "initial": true, "description": "삭제 영역을 오버레이한다"},
    ]},
]

function tool(key) {
    return tools.find(function(entry) { return entry.key === key }) || null
}

// Preserve the original Figma catalog for design comparison. The native editor
// names its actual operations and omits modes with no connected implementation.
function runtimeDefinition(source) {
    if (!source) return null
    var definition = Object.assign({}, source)
    if (source.selector) {
        var unavailableModes = source.key === "camera-photo" ? ["RAW Capture"]
            : source.key === "layers" ? ["Dynamic", "Group"]
            : source.key === "retouch" ? ["Face Refine"] : []
        definition.selector = Object.assign({}, source.selector, {
            options: source.selector.options.filter(function(mode) { return unavailableModes.indexOf(mode) < 0 })
        })
    }
    definition.fields = source.fields.map(function(field) {
        if (source.key === "effects" && field.id === "field-45")
            return Object.assign({}, field, {type: "Slider", label: "Manual distortion", initial: 0, minimum: -100, maximum: 100, step: 1, unit: "%", decimals: 0})
        if (source.key === "effects" && field.id === "field-46")
            return Object.assign({}, field, {label: "Reduce color fringe", initial: false, description: "수동으로 색수차를 줄인다"})
        if (source.key === "layers" && field.id === "field-19")
            return Object.assign({}, field, {label: "Layer name", initial: ""})
        if (source.key === "file" && field.id === "field-12")
            return Object.assign({}, field, {label: "Save as…"})
        if (source.key === "asset" && field.id === "field-11")
            return Object.assign({}, field, {label: "Import source…"})
        if (source.key === "asset" && field.id === "field-5")
            return Object.assign({}, field, {options: ["Recent", "Name"], initial: "Recent"})
        if (source.key === "text" && field.id === "field-2")
            return Object.assign({}, field, {unit: "px"})
        if (source.key === "generative" && field.id === "field-22")
            return Object.assign({}, field, {label: "Choose reference…"})
        if (source.key === "effects" && field.id === "field-32")
            return Object.assign({}, field, {options: ["Uniform", "Film"], initial: "Uniform"})
        if (source.key === "masking" && field.id === "field-14")
            return Object.assign({}, field, {options: ["Sky", "Background"], initial: "Sky"})
        if (source.key === "masking" && field.id === "field-15")
            return Object.assign({}, field, {label: "Semantic region preview"})
        if (source.key === "retouch" && field.id === "field-14")
            return Object.assign({}, field, {label: "Follow stroke", description: "스트로크 내 소스 오프셋을 유지한다"})
        if (source.key === "select" && field.id === "field-7")
            return Object.assign({}, field, {label: "Snap to 8 px grid", description: "8픽셀 격자에 맞춘다"})
        return field
    })
    return definition
}

// A selector chooses the operation to configure. It must change the displayed
// controls, rather than leaving unrelated parameters in an apparently active UI.
function applicableFields(definition, values) {
    var mode = values.selector || (definition.selector ? definition.selector.initial : "")
    var groups = {
        "elements": {common: [0, 2, 3, 6, 10, 14, 15, 16, 17], "Rectangle": [1, 8], "Ellipse": [4, 5], "Polygon": [5, 7, 8, 9], "Line / Arrow": [11, 12, 13]},
        "text": {common: [0, 1, 2, 3], "Free Text": [], "Caption": [4, 5, 6], "Header Title": [7, 8, 9], "Callout": [10, 11, 12], "Footer": [13, 14]},
        "camera-photo": {common: [], "Camera": [0, 1, 3], "Photo Library": [7], "Document Scan": [7, 9, 10, 11]},
        "generative": {common: [0, 1, 2, 23, 24, 25, 27, 28, 30], "Text-to-image": [3], "Image-to-image": [4, 5, 7, 22], "Inpaint": [4, 5, 8, 9, 10, 22], "Outpaint": [4, 5, 11, 12, 14, 22]},
        "layers": {common: [8, 9, 10, 19, 20], "Generated": [0, 1, 3], "Pixel": [11], "Vector": [12, 13, 15], "Adjustment": [16, 18], "Mask": [23, 25, 26]},
        "select": {common: [0], "Lasso": [1, 2, 3], "Rectangle": [2, 4, 5, 6], "Triangle": [2, 6, 7, 8], "Magic Wand": [9, 10, 11, 12], "Object": [13, 14, 15, 16]},
        "retouch": {common: [0, 4], "Remove": [2, 3, 5, 6], "Inpaint": [5, 6, 7], "Heal": [8, 9, 10, 11, 12, 13, 14, 15, 16, 17], "Clone": [8, 10, 11, 12, 13, 14, 15], "Content-aware": [3]},
        "fill": {common: [], "Solid": [0, 1, 2, 3], "Gradient": [1, 2, 3, 4, 5, 6, 7], "Pattern": [1, 2, 3, 8, 9, 10, 11], "Generative": [12, 13, 15]},
        "masking": {common: [6, 7, 22, 23, 24], "Brush": [0, 1, 2, 3], "Linear": [4, 5], "Radial": [8, 9], "Subject": [10, 11, 12, 13], "Region": [14, 15], "Range": [18, 19, 20, 21]},
        "eraser": {common: [], "Pixel": [0, 1, 2, 3, 13], "Object": [0, 5, 6, 7], "Vector": [0, 8, 10], "Restore / Protect": [0, 11, 12, 13]}
    }
    var group = groups[definition.key]
    if (!group || !definition.selector) return definition.fields
    var allowed = group.common.concat(group[mode] || [])
    return definition.fields.filter(function(field) { return allowed.indexOf(Number(field.id.slice(6))) >= 0 })
}

function defaults(definition) {
    var result = {}
    if (!definition) return result
    if (definition.selector) result.selector = definition.selector.initial
    definition.fields.forEach(function(field) {
        result[field.id] = Array.isArray(field.initial) ? field.initial.slice() : field.initial
    })
    return result
}

// Match the paired slider/switch rows, preserving the Figma field order.
function rows(definition, compact) {
    var result = [], pending = null
    if (!definition) return result
    definition.fields.forEach(function(field) {
        if (field.type === "Action") return
        var half = !compact && (field.type === "Slider" || field.type === "Toggle") && field.label.length <= 25
        if (!half) { pending = null; result.push([field]) }
        else if (pending) { pending.push(field); pending = null }
        else { pending = [field]; result.push(pending) }
    })
    return result
}

// Keep button order while wrapping each row against the parameter edge.
// Both choices and actions use their LVRS intrinsic widths.
function buttonRows(width, widths, columns, height, gap, rightAligned) {
    var items = [], row = [], rowWidth = 0, y = 0
    function finishRow() {
        if (!row.length) return
        var x = rightAligned ? Math.max(0, width - rowWidth) : 0
        row.forEach(function(entry) {
            items[entry.index] = {x: x, y: y, width: entry.width}
            x += entry.width + gap
        })
        y += height + gap
        row = []; rowWidth = 0
    }
    widths.forEach(function(naturalWidth, index) {
        var itemWidth = Math.min(Math.max(0, width), naturalWidth)
        if (row.length && (row.length >= columns || rowWidth + gap + itemWidth > width)) finishRow()
        rowWidth += (row.length ? gap : 0) + itemWidth
        row.push({index: index, width: itemWidth})
    })
    finishRow()
    return {items: items, height: items.length ? y - gap : 0}
}

function numberText(field, value, desktop) {
    if (field.label === "Pan" && value === 0) return "Center"
    var precision = field.decimals
    // Preserve entered precision even when the design's initial value uses fewer decimals.
    if (desktop) {
        while (precision < 8 && Math.abs(Number(Number(value).toFixed(precision)) - value) > 0.00000001)
            ++precision
    }
    var text = Number(value).toFixed(precision)
    if (desktop && field.desktopPositiveSign && value > 0) text = "+" + text
    if (desktop) text = text.replace(/^-/, "−")
    return text + (field.unit === "%" || field.unit === "°" ? "" : field.unit ? " " : "") + field.unit
}

function formatted(field, value, desktop) {
    return Array.isArray(value) ? value[0] + " — " + value[1] + field.unit : numberText(field, value, desktop)
}

// Reject malformed/out-of-range values; never replace a draft with NaN or a partial number.
function parsed(field, text) {
    var cleaned = text.trim().replace(/−/g, "-")
    if (field.label === "Pan" && cleaned === "Center") return 0
    if (field.unit && cleaned.endsWith(field.unit)) cleaned = cleaned.slice(0, -field.unit.length).trim()
    var parts = Array.isArray(field.initial) ? cleaned.split(/\s*—\s*/) : [cleaned]
    if (parts.length !== (Array.isArray(field.initial) ? 2 : 1)) return null
    var values = []
    for (var i = 0; i < parts.length; ++i) {
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(parts[i])) return null
        var value = Number(parts[i])
        if (!isFinite(value) || value < field.minimum || value > field.maximum) return null
        var snapped = Math.round(value / field.step) * field.step
        if (Math.abs(snapped - value) > 0.00001) return null
        values.push(value)
    }
    if (values.length === 2 && values[0] > values[1]) return null
    return values.length === 2 ? values : values[0]
}
