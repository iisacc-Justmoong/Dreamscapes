.pragma library

// Product role names resolve to the shared LVRS icon library.
var icons = {
    "Text": {
        "AlignBottom": "align-bottom",
        "AlignCenter": "align-center",
        "AlignMiddle": "align-middle",
        "AlignTop": "align-top",
        "AreaText": "area-text",
        "AutoRefresh": "autoRefresh",
        "Callout": "callout",
        "CenterAlign": "centerAlign",
        "Comment": "comment",
        "Font": "font",
        "FontSize": "font-size",
        "LeftAlign": "leftAlign",
        "ParagraphJustify": "paragraph-justify",
        "RightAlign": "rightAlign",
        "Text": "text"
    },
    "Layers": {
        "AdjustmentLayer": "adjustment-layer",
        "AutoTone": "auto-tone",
        "BlackWhite": "black-white",
        "Contrast": "contrast",
        "Curves": "curves",
        "DuplicateLayer": "duplicate-layer",
        "Exposure": "exposure",
        "Eyedropper": "eyedropper",
        "Folder": "folder",
        "GeneralchevronDown": "generalchevronDown",
        "Generaldelete": "generaldelete",
        "Generalrefresh": "generalrefresh",
        "Gradient": "gradient",
        "Group": "group",
        "Highlights": "highlights",
        "LayerMask": "layer-mask",
        "Levels": "levels",
        "Lock": "lock",
        "Merge": "merge",
        "Metadata": "metadata",
        "NewLayer": "new-layer",
        "ProofColors": "proof-colors",
        "Saturation": "saturation",
        "Shadows": "shadows",
        "StatusInfoOutline": "statusinfoOutline",
        "Vectorize": "vectorize",
        "Vibrance": "vibrance",
        "Visibility": "visibility",
        "WhiteBalance": "white-balance",
        "ImageGutter14X14": "imageGutter@14x14"
    },
    "Graphic": {
        "Adjustment": "adjustment",
        "Blur": "blur",
        "Color": "color",
        "ColorGrading": "color-grading",
        "ColorPalette": "color-palette",
        "ColorPicker": "color-picker",
        "Histogram": "histogram",
        "LensCorrection": "lens-correction",
        "LocalAdjustment": "local-adjustment",
        "NoiseEffect": "noise-effect",
        "Sharpen": "sharpen",
        "ToneCurveChannel": "tone-curve-channel",
        "reset": "reset",
        "Filter": "generalfilter",
        "adjust": "adjustment"
    },
    "Elements": {
        "direct-select": "direct-select",
        "ellipse": "ellipse",
        "fill": "fill",
        "group": "group",
        "line": "line",
        "polygon": "polygon",
        "rectangle": "rectangle",
        "stroke": "stroke",
        "auto-refresh": "autoRefresh",
        "unselect-all": "unselectAll",
        "arrow-down-tone-primary": "generalchevronDownAccent",
        "arrow-down-tone-borderless": "generalchevronDownBorderless",
        "arrow-up-tone-primary": "generalchevronUp",
        "arrow-up-tone-borderless": "generalchevronUp",
        "arrow-up-down-tone-primary": "StepperUpDownChevron",
        "arrow-up-down-tone-borderless": "StepperUpDownChevron"
    },
    "CameraPhoto": {
        "panel-camera-live": "panel-camera-live",
        "panel-photo-library": "panel-photo-library",
        "panel-photo-source": "panel-photo-source",
        "sort-assets": "sort-assets",
        "transform-selection": "transform-selection",
        "preview-camera": "panel-camera-live"
    },
    "File": {
        "document-properties": "document-properties",
        "panel-file-location": "panel-file-location",
        "panel-preserve-layers": "panel-preserve-layers"
    }
}

function resolve(theme, group, name) {
    const icon = icons[group] && icons[group][name]
    return icon ? theme.iconPath(icon) : ""
}
