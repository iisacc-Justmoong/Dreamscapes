// Figma 772:80714. Presentation only; field IDs keep the native editing contract.
.pragma library

var tools = {
    "audio-track": {
        "node": "777:83338",
        "views": [
            {
                "id": "import",
                "title": "Import",
                "icon": "panel-audio-import",
                "node": "777:83860",
                "height": 296,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "777:83951",
                        "label": "File",
                        "height": 22,
                        "icon": "panel-audio-import"
                    },
                    {
                        "id": "field-1",
                        "node": "777:83955",
                        "label": "Channels",
                        "height": 54,
                        "icon": "panel-audio-channels",
                        "options": [
                            "Stereo",
                            "Mono",
                            "Split"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-2",
                        "node": "777:83966",
                        "label": "Sample rate",
                        "height": 54,
                        "icon": "panel-sample-rate",
                        "options": [
                            "44.1k",
                            "48k",
                            "96k"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-3",
                        "node": "777:83977",
                        "label": "Import track",
                        "height": 22,
                        "icon": "panel-audio-import"
                    }
                ]
            },
            {
                "id": "trim",
                "title": "Trim",
                "icon": "panel-audio-trim",
                "node": "777:83985",
                "height": 314,
                "fields": [
                    {
                        "id": "field-4",
                        "node": "777:84075",
                        "label": "Waveform / selection",
                        "height": 44
                    },
                    {
                        "id": "field-5",
                        "node": "777:84077",
                        "label": "Range",
                        "height": 22,
                        "icon": "panel-trim-range"
                    },
                    {
                        "id": "field-6",
                        "node": "777:84081",
                        "label": "Fade in",
                        "height": 52,
                        "icon": "fade-in"
                    },
                    {
                        "id": "field-7",
                        "node": "777:84088",
                        "label": "Fade out",
                        "height": 52,
                        "icon": "fade-out"
                    }
                ]
            },
            {
                "id": "mix",
                "title": "Mix",
                "icon": "panel-audio-mix",
                "node": "777:84100",
                "height": 322,
                "fields": [
                    {
                        "id": "field-8",
                        "node": "777:84193",
                        "label": "Volume",
                        "height": 52,
                        "icon": "volume"
                    },
                    {
                        "id": "field-9",
                        "node": "777:84200",
                        "label": "Pan",
                        "height": 52,
                        "icon": "pan"
                    },
                    {
                        "id": "field-10",
                        "node": "777:84207",
                        "label": "Auto ducking",
                        "height": 22,
                        "icon": "panel-audio-ducking"
                    },
                    {
                        "id": "field-11",
                        "node": "777:84213",
                        "label": "Ducking amount",
                        "height": 52,
                        "icon": "panel-ducking-amount"
                    }
                ]
            },
            {
                "id": "sync",
                "title": "Sync",
                "icon": "panel-audio-sync",
                "node": "777:84225",
                "height": 294,
                "fields": [
                    {
                        "id": "field-12",
                        "node": "777:84316",
                        "label": "Sync to",
                        "height": 54,
                        "icon": "panel-sync-target",
                        "options": [
                            "Timeline",
                            "Beat",
                            "Markers"
                        ],
                        "desktopColumns": 3,
                        "disabledOptions": [
                            "Markers"
                        ]
                    },
                    {
                        "id": "field-13",
                        "node": "777:84327",
                        "label": "Offset",
                        "height": 52,
                        "icon": "panel-time-offset"
                    },
                    {
                        "id": "field-14",
                        "node": "777:84334",
                        "label": "Stretch to fit",
                        "height": 22,
                        "icon": "panel-audio-stretch"
                    },
                    {
                        "id": "field-15",
                        "node": "777:84340",
                        "label": "Analyze & sync",
                        "height": 22,
                        "icon": "panel-audio-sync"
                    }
                ]
            }
        ]
    },
    "canvas": {
        "node": "777:84609",
        "views": [
            {
                "id": "size-resolution",
                "title": "Size & Resolution",
                "icon": "panel-canvas-dimensions",
                "node": "777:85233",
                "height": 289,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "777:85339",
                        "label": "Dimensions",
                        "height": 47
                    },
                    {
                        "id": "field-1",
                        "node": "777:85344",
                        "label": "Resolution",
                        "height": 22,
                        "icon": "panel-resolution-grid"
                    },
                    {
                        "id": "field-2",
                        "node": "777:85348",
                        "label": "Resample",
                        "height": 22,
                        "icon": "panel-resample-grid"
                    },
                    {
                        "id": "field-3",
                        "node": "777:85354",
                        "label": "Anchor",
                        "height": 54,
                        "icon": "panel-anchor-point",
                        "options": [
                            "Center",
                            "Top",
                            "Custom"
                        ],
                        "desktopColumns": 3,
                        "disabledOptions": [
                            "Top",
                            "Custom"
                        ]
                    }
                ]
            },
            {
                "id": "color-profile",
                "title": "Color Profile",
                "icon": "panel-canvas-profile",
                "node": "777:85371",
                "height": 296,
                "fields": [
                    {
                        "id": "field-4",
                        "node": "777:85479",
                        "label": "Profile",
                        "height": 54,
                        "icon": "panel-canvas-profile",
                        "options": [
                            "Display P3",
                            "sRGB",
                            "Adobe RGB"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-5",
                        "node": "777:85490",
                        "label": "Intent",
                        "height": 54,
                        "icon": "panel-rendering-intent",
                        "options": [
                            "Relative",
                            "Perceptual",
                            "Absolute"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-6",
                        "node": "777:85501",
                        "label": "Black point compensation",
                        "height": 22,
                        "icon": "panel-black-point"
                    },
                    {
                        "id": "field-7",
                        "node": "777:85507",
                        "label": "Convert profile",
                        "height": 22,
                        "icon": "panel-canvas-profile"
                    }
                ]
            },
            {
                "id": "rotate",
                "title": "Rotate",
                "icon": "panel-canvas-rotate",
                "node": "777:85516",
                "height": 294,
                "fields": [
                    {
                        "id": "field-8",
                        "node": "777:85622",
                        "label": "Angle",
                        "height": 54,
                        "icon": "rotate",
                        "options": [
                            "90° L",
                            "180°",
                            "90° R"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-9",
                        "node": "777:85633",
                        "label": "Custom angle",
                        "height": 52,
                        "icon": "panel-canvas-rotate"
                    },
                    {
                        "id": "field-10",
                        "node": "777:85640",
                        "label": "Rotate layers",
                        "height": 22,
                        "icon": "panel-rotate-layers"
                    },
                    {
                        "id": "field-11",
                        "node": "777:85646",
                        "label": "Apply rotation",
                        "height": 22,
                        "icon": "panel-canvas-rotate"
                    }
                ]
            },
            {
                "id": "mirror",
                "title": "Mirror",
                "icon": "panel-canvas-mirror",
                "node": "777:85655",
                "height": 264,
                "fields": [
                    {
                        "id": "field-12",
                        "node": "777:85763",
                        "label": "Axis",
                        "height": 54,
                        "icon": "panel-canvas-mirror",
                        "options": [
                            "Horizontal",
                            "Vertical"
                        ],
                        "desktopColumns": 2
                    },
                    {
                        "id": "field-13",
                        "node": "777:85772",
                        "label": "Mirror canvas only",
                        "height": 22,
                        "icon": "panel-mirror-only"
                    },
                    {
                        "id": "field-14",
                        "node": "777:85778",
                        "label": "Keep text readable",
                        "height": 22,
                        "icon": "panel-readable-text"
                    },
                    {
                        "id": "field-15",
                        "node": "777:85784",
                        "label": "Mirror canvas",
                        "height": 22,
                        "icon": "panel-canvas-mirror"
                    }
                ]
            },
            {
                "id": "crop-guides",
                "title": "Crop & Guides",
                "icon": "panel-canvas-guides",
                "node": "777:85793",
                "height": 264,
                "fields": [
                    {
                        "id": "field-16",
                        "node": "777:85899",
                        "label": "Overlay",
                        "height": 54,
                        "icon": "panel-guide-overlay",
                        "options": [
                            "Crop",
                            "Bleed",
                            "Safe"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-17",
                        "node": "777:85910",
                        "label": "Bleed",
                        "height": 22,
                        "icon": "panel-bleed-area"
                    },
                    {
                        "id": "field-18",
                        "node": "777:85914",
                        "label": "Safe inset",
                        "height": 22,
                        "icon": "panel-safe-inset"
                    },
                    {
                        "id": "field-19",
                        "node": "777:85918",
                        "label": "Delete cropped pixels",
                        "height": 22,
                        "icon": "panel-delete-cropped"
                    }
                ]
            }
        ]
    },
    "generative": {
        "node": "778:83698",
        "views": [
            {
                "id": "text-to-image",
                "title": "Text-to-image",
                "icon": "panel-generate-text-image",
                "node": "778:84712",
                "height": 264,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "778:84853",
                        "label": "Prompt",
                        "height": 22,
                        "icon": "panel-prompt-text"
                    },
                    {
                        "id": "field-1",
                        "node": "778:84857",
                        "label": "Negative",
                        "height": 22,
                        "icon": "panel-negative-prompt"
                    },
                    {
                        "id": "field-2",
                        "node": "778:84861",
                        "label": "Model",
                        "height": 54,
                        "icon": "panel-model-chip",
                        "options": [
                            "Standard",
                            "Artwork",
                            "Photo"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-3",
                        "node": "778:84872",
                        "label": "Generate 4 variations",
                        "height": 22,
                        "icon": "panel-generate-text-image"
                    }
                ],
                "mode": "Text-to-image"
            },
            {
                "id": "image-to-image",
                "title": "Image-to-image",
                "icon": "panel-generate-image-image",
                "node": "778:84884",
                "height": 326,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "778:85023",
                        "label": "Prompt",
                        "height": 22,
                        "icon": "panel-prompt-text"
                    },
                    {
                        "id": "field-4",
                        "node": "778:85027",
                        "label": "Reference",
                        "height": 22,
                        "icon": "panel-generate-reference"
                    },
                    {
                        "id": "field-5",
                        "node": "778:85031",
                        "label": "Denoise strength",
                        "height": 52,
                        "icon": "panel-denoise"
                    },
                    {
                        "id": "field-6",
                        "node": "778:85038",
                        "label": "Structure lock",
                        "height": 52,
                        "icon": "panel-structure-lock"
                    },
                    {
                        "id": "field-7",
                        "node": "778:85045",
                        "label": "Generate from image",
                        "height": 22,
                        "icon": "panel-generate-image-image"
                    }
                ],
                "mode": "Image-to-image"
            },
            {
                "id": "inpaint",
                "title": "Inpaint",
                "icon": "panel-generate-inpaint",
                "node": "778:85057",
                "height": 284,
                "fields": [
                    {
                        "id": "field-8",
                        "node": "778:85196",
                        "label": "Inpaint mask",
                        "height": 44
                    },
                    {
                        "id": "field-0",
                        "node": "778:85198",
                        "label": "Prompt",
                        "height": 22,
                        "icon": "panel-prompt-text"
                    },
                    {
                        "id": "field-9",
                        "node": "778:85202",
                        "label": "Mask feather",
                        "height": 52,
                        "icon": "mask-feather"
                    },
                    {
                        "id": "field-10",
                        "node": "778:85209",
                        "label": "Inpaint selection",
                        "height": 22,
                        "icon": "panel-generate-inpaint"
                    }
                ],
                "mode": "Inpaint"
            },
            {
                "id": "expand",
                "title": "Expand",
                "icon": "panel-generate-expand",
                "node": "778:85221",
                "height": 264,
                "fields": [
                    {
                        "id": "field-11",
                        "node": "778:85360",
                        "label": "Direction",
                        "height": 54,
                        "icon": "panel-expand-direction",
                        "options": [
                            "All",
                            "Left/Right",
                            "Top/Bottom"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-12",
                        "node": "778:85371",
                        "label": "Expansion",
                        "height": 22,
                        "icon": "panel-generate-expand"
                    },
                    {
                        "id": "field-13",
                        "node": "778:85375",
                        "label": "Match perspective",
                        "height": 22,
                        "icon": "panel-perspective-match"
                    },
                    {
                        "id": "field-14",
                        "node": "778:85381",
                        "label": "Expand canvas",
                        "height": 22,
                        "icon": "panel-generate-expand"
                    }
                ],
                "mode": "Outpaint"
            },
            {
                "id": "control",
                "title": "Control",
                "icon": "panel-generate-control",
                "node": "778:85393",
                "height": 294,
                "fields": [
                    {
                        "id": "field-15",
                        "node": "778:85531",
                        "label": "Control",
                        "height": 54,
                        "icon": "panel-generate-control",
                        "options": [
                            "Canny",
                            "Depth",
                            "Pose",
                            "Lineart"
                        ],
                        "desktopColumns": 4
                    },
                    {
                        "id": "field-16",
                        "node": "778:85544",
                        "label": "Weight",
                        "height": 52,
                        "icon": "panel-control-weight"
                    },
                    {
                        "id": "field-17",
                        "node": "778:85551",
                        "label": "Guidance range",
                        "height": 22,
                        "icon": "panel-guidance-range"
                    },
                    {
                        "id": "field-18",
                        "node": "778:85555",
                        "label": "Pixel perfect",
                        "height": 22,
                        "icon": "panel-pixel-perfect"
                    }
                ]
            },
            {
                "id": "reference",
                "title": "Reference",
                "icon": "panel-generate-reference",
                "node": "778:85570",
                "height": 324,
                "fields": [
                    {
                        "id": "field-19",
                        "node": "778:85709",
                        "label": "Mode",
                        "height": 54,
                        "icon": "panel-reference-mode",
                        "options": [
                            "Image",
                            "Face",
                            "Style"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-20",
                        "node": "778:85720",
                        "label": "Reference weight",
                        "height": 52,
                        "icon": "panel-reference-weight"
                    },
                    {
                        "id": "field-21",
                        "node": "778:85727",
                        "label": "Style fidelity",
                        "height": 52,
                        "icon": "panel-style-fidelity"
                    },
                    {
                        "id": "field-22",
                        "node": "778:85734",
                        "label": "Add reference",
                        "height": 22,
                        "icon": "panel-generate-reference"
                    }
                ]
            },
            {
                "id": "model-lora",
                "title": "Model & LoRA",
                "icon": "panel-generate-model-lora",
                "node": "778:85746",
                "height": 262,
                "fields": [
                    {
                        "id": "field-23",
                        "node": "778:85884",
                        "label": "LoRA",
                        "height": 22,
                        "icon": "panel-lora-module"
                    },
                    {
                        "id": "field-24",
                        "node": "778:85888",
                        "label": "LoRA weight",
                        "height": 52,
                        "icon": "panel-lora-weight"
                    },
                    {
                        "id": "field-25",
                        "node": "778:85895",
                        "label": "VAE",
                        "height": 22,
                        "icon": "panel-vae-codec"
                    },
                    {
                        "id": "field-26",
                        "node": "778:85899",
                        "label": "Bake into result",
                        "height": 22,
                        "icon": "panel-bake-result"
                    }
                ]
            },
            {
                "id": "upscale",
                "title": "Upscale",
                "icon": "panel-generate-upscale",
                "node": "778:85914",
                "height": 326,
                "fields": [
                    {
                        "id": "field-27",
                        "node": "778:86053",
                        "label": "Scale",
                        "height": 54,
                        "icon": "panel-generate-upscale",
                        "options": [
                            "2×",
                            "4×",
                            "8×"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-28",
                        "node": "778:86064",
                        "label": "Method",
                        "height": 54,
                        "icon": "panel-upscale-method",
                        "options": [
                            "Detail",
                            "Photo",
                            "Artwork"
                        ],
                        "desktopColumns": 3,
                        "unsupported": true
                    },
                    {
                        "id": "field-29",
                        "node": "778:86075",
                        "label": "Refiner strength",
                        "height": 52,
                        "icon": "panel-refiner-strength"
                    },
                    {
                        "id": "field-30",
                        "node": "778:86082",
                        "label": "Upscale layer",
                        "height": 22,
                        "icon": "panel-generate-upscale"
                    }
                ]
            }
        ]
    },
    "select": {
        "node": "778:88804",
        "views": [
            {
                "id": "lasso",
                "title": "Lasso",
                "icon": "panel-select-lasso",
                "mode": "Lasso",
                "node": "778:88804",
                "height": 294,
                "fields": [
                    {
                        "id": "field-0",
                        "label": "Mode",
                        "icon": "panel-selection-mode",
                        "height": 54
                    },
                    {
                        "id": "field-1",
                        "label": "Smoothing",
                        "icon": "brush-smoothing",
                        "height": 52
                    },
                    {
                        "id": "field-2",
                        "label": "Feather",
                        "icon": "feather",
                        "height": 52
                    },
                    {
                        "id": "field-3",
                        "label": "Close automatically",
                        "icon": "panel-select-lasso",
                        "height": 22
                    }
                ]
            },
            {
                "id": "rectangle",
                "title": "Rectangle",
                "icon": "panel-select-rectangle",
                "node": "778:89768",
                "height": 390,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "778:89864",
                        "label": "Mode",
                        "height": 54,
                        "icon": "panel-selection-mode",
                        "options": [
                            "New",
                            "Add",
                            "Subtract"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-4",
                        "node": "778:89875",
                        "label": "Ratio",
                        "height": 54,
                        "icon": "panel-aspect-ratio",
                        "options": [
                            "Free",
                            "1:1",
                            "4:5"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-5",
                        "node": "778:89886",
                        "label": "From center",
                        "height": 22,
                        "icon": "panel-from-center"
                    },
                    {
                        "id": "field-6",
                        "node": "778:89892",
                        "label": "Rotation",
                        "height": 52,
                        "icon": "rotate"
                    },
                    {
                        "id": "field-2",
                        "node": "778:89899",
                        "label": "Feather",
                        "height": 52,
                        "icon": "feather"
                    }
                ],
                "mode": "Rectangle"
            },
            {
                "id": "triangle",
                "title": "Triangle",
                "icon": "panel-select-polygon",
                "node": "778:89912",
                "height": 388,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "778:90007",
                        "label": "Mode",
                        "height": 54,
                        "icon": "panel-selection-mode",
                        "options": [
                            "New",
                            "Add",
                            "Subtract"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-6",
                        "node": "778:90018",
                        "label": "Rotation",
                        "height": 52,
                        "icon": "rotate"
                    },
                    {
                        "id": "field-7",
                        "node": "778:90025",
                        "label": "Snap vertices",
                        "height": 22,
                        "icon": "panel-vertex-snap"
                    },
                    {
                        "id": "field-8",
                        "node": "778:90031",
                        "label": "Corner roundness",
                        "height": 52,
                        "icon": "panel-corner-roundness"
                    },
                    {
                        "id": "field-2",
                        "node": "778:90038",
                        "label": "Feather",
                        "height": 52,
                        "icon": "feather"
                    }
                ],
                "mode": "Triangle"
            },
            {
                "id": "magic-wand",
                "title": "Magic Wand",
                "icon": "panel-select-magic",
                "node": "778:90051",
                "height": 358,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "778:90148",
                        "label": "Mode",
                        "height": 54,
                        "icon": "panel-selection-mode",
                        "options": [
                            "New",
                            "Add",
                            "Subtract"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-9",
                        "node": "778:90159",
                        "label": "Tolerance",
                        "height": 52,
                        "icon": "panel-color-tolerance"
                    },
                    {
                        "id": "field-10",
                        "node": "778:90166",
                        "label": "Contiguous",
                        "height": 22,
                        "icon": "panel-contiguous"
                    },
                    {
                        "id": "field-11",
                        "node": "778:90172",
                        "label": "Sample all layers",
                        "height": 22,
                        "icon": "panel-sample-layers"
                    },
                    {
                        "id": "field-12",
                        "node": "778:90178",
                        "label": "Edge refine",
                        "height": 52,
                        "icon": "panel-edge-refine"
                    }
                ],
                "mode": "Magic Wand"
            },
            {
                "id": "object",
                "title": "Object",
                "icon": "panel-select-object",
                "node": "778:90191",
                "height": 382,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "778:90287",
                        "label": "Mode",
                        "height": 54,
                        "icon": "panel-selection-mode",
                        "options": [
                            "New",
                            "Add",
                            "Subtract"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-13",
                        "node": "778:90298",
                        "label": "Target",
                        "height": 54,
                        "icon": "panel-target-subject",
                        "options": [
                            "Object",
                            "Person",
                            "Part"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-14",
                        "node": "778:90309",
                        "label": "Detected objects",
                        "height": 44
                    },
                    {
                        "id": "field-15",
                        "node": "778:90311",
                        "label": "Confidence",
                        "height": 52,
                        "icon": "panel-confidence"
                    },
                    {
                        "id": "field-16",
                        "node": "778:90318",
                        "label": "Select detected",
                        "height": 22,
                        "icon": "panel-select-object"
                    }
                ],
                "mode": "Object"
            }
        ]
    },
    "retouch": {
        "node": "779:86589",
        "views": [
            {
                "id": "remove",
                "title": "Remove",
                "icon": "panel-retouch-remove",
                "node": "779:87604",
                "height": 294,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "779:87723",
                        "label": "Brush size",
                        "height": 52,
                        "icon": "brush-size"
                    },
                    {
                        "id": "field-1",
                        "node": "779:87730",
                        "label": "Detect object",
                        "height": 22,
                        "icon": "panel-select-object"
                    },
                    {
                        "id": "field-2",
                        "node": "779:87736",
                        "label": "Fill",
                        "height": 54,
                        "icon": "panel-fill-strategy",
                        "options": [
                            "Auto",
                            "Content",
                            "Generate"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-3",
                        "node": "779:87747",
                        "label": "Remove marked object",
                        "height": 22,
                        "icon": "panel-retouch-remove"
                    }
                ],
                "mode": "Remove"
            },
            {
                "id": "inpaint",
                "title": "Inpaint",
                "icon": "panel-retouch-inpaint",
                "node": "779:87757",
                "height": 412,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "779:87875",
                        "label": "Brush size",
                        "height": 52,
                        "icon": "brush-size"
                    },
                    {
                        "id": "field-4",
                        "node": "779:87882",
                        "label": "Repair mask",
                        "height": 44
                    },
                    {
                        "id": "field-5",
                        "node": "779:87884",
                        "label": "Prompt",
                        "height": 22,
                        "icon": "panel-prompt-text"
                    },
                    {
                        "id": "field-6",
                        "node": "779:87888",
                        "label": "Context strength",
                        "height": 52,
                        "icon": "panel-context-strength"
                    },
                    {
                        "id": "field-8",
                        "node": "779:87895",
                        "label": "Feather",
                        "height": 52,
                        "icon": "feather"
                    },
                    {
                        "id": "field-7",
                        "node": "779:87902",
                        "label": "Inpaint area",
                        "height": 22,
                        "icon": "panel-retouch-inpaint"
                    }
                ],
                "mode": "Inpaint"
            },
            {
                "id": "heal",
                "title": "Heal",
                "icon": "panel-retouch-heal",
                "node": "779:87912",
                "height": 324,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "779:88031",
                        "label": "Brush size",
                        "height": 52,
                        "icon": "brush-size"
                    },
                    {
                        "id": "field-9",
                        "node": "779:88038",
                        "label": "Source",
                        "height": 54,
                        "icon": "panel-clone-source",
                        "options": [
                            "Auto",
                            "Pick",
                            "Pattern"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-10",
                        "node": "779:88049",
                        "label": "Sample all layers",
                        "height": 22,
                        "icon": "panel-sample-layers"
                    },
                    {
                        "id": "field-8",
                        "node": "779:88055",
                        "label": "Feather",
                        "height": 52,
                        "icon": "feather"
                    }
                ],
                "mode": "Heal"
            },
            {
                "id": "clone",
                "title": "Clone",
                "icon": "panel-retouch-clone",
                "node": "779:88069",
                "height": 448,
                "fields": [
                    {
                        "id": "field-9",
                        "node": "779:88187",
                        "label": "Source",
                        "height": 54,
                        "icon": "panel-clone-source",
                        "options": [
                            "Auto",
                            "Pick",
                            "Pattern"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-12",
                        "node": "779:88198",
                        "label": "Scale",
                        "height": 52,
                        "icon": "brush-size"
                    },
                    {
                        "id": "field-13",
                        "node": "779:88205",
                        "label": "Rotation",
                        "height": 52,
                        "icon": "rotate"
                    },
                    {
                        "id": "field-14",
                        "node": "779:88212",
                        "label": "Aligned",
                        "height": 22,
                        "icon": "panel-clone-aligned"
                    },
                    {
                        "id": "field-15",
                        "node": "779:88218",
                        "label": "Sampling overlay",
                        "height": 44
                    },
                    {
                        "id": "field-10",
                        "node": "779:88220",
                        "label": "Sample all layers",
                        "height": 22,
                        "icon": "panel-sample-layers"
                    },
                    {
                        "id": "field-11",
                        "node": "779:88226",
                        "label": "Set clone source",
                        "height": 22,
                        "icon": "panel-retouch-clone"
                    }
                ],
                "mode": "Clone"
            },
            {
                "id": "content-aware",
                "title": "Content-aware",
                "icon": "panel-retouch-content-aware",
                "node": "779:88236",
                "height": 324,
                "fields": [
                    {
                        "id": "field-16",
                        "node": "779:88356",
                        "label": "Adaptation",
                        "height": 54,
                        "icon": "panel-content-adaptation",
                        "options": [
                            "Auto",
                            "Color",
                            "Rotation"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-17",
                        "node": "779:88367",
                        "label": "Color adaptation",
                        "height": 52,
                        "icon": "panel-style-fidelity"
                    },
                    {
                        "id": "field-8",
                        "node": "779:88374",
                        "label": "Feather",
                        "height": 52,
                        "icon": "feather"
                    },
                    {
                        "id": "field-18",
                        "node": "779:88381",
                        "label": "Preview fill",
                        "height": 22,
                        "icon": "panel-retouch-content-aware"
                    }
                ],
                "mode": "Content-aware"
            },
            {
                "id": "face-refine",
                "title": "Face Refine",
                "icon": "panel-retouch-face",
                "node": "779:88391",
                "height": 324,
                "fields": [
                    {
                        "id": "field-19",
                        "node": "779:88509",
                        "label": "Region",
                        "height": 54,
                        "icon": "panel-retouch-face",
                        "options": [
                            "Skin",
                            "Eyes",
                            "Teeth"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-20",
                        "node": "779:88520",
                        "label": "Skin smoothing",
                        "height": 52,
                        "icon": "panel-skin-smooth"
                    },
                    {
                        "id": "field-21",
                        "node": "779:88527",
                        "label": "Eye detail",
                        "height": 52,
                        "icon": "panel-eye-detail"
                    },
                    {
                        "id": "field-22",
                        "node": "779:88534",
                        "label": "Preserve identity",
                        "height": 22,
                        "icon": "panel-identity-lock"
                    }
                ],
                "mode": "Face Refine"
            }
        ]
    },
    "fill": {
        "node": "779:89044",
        "views": [
            {
                "id": "solid",
                "title": "Solid",
                "icon": "panel-fill-solid",
                "node": "779:89796",
                "height": 316,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "779:89894",
                        "label": "Color palette",
                        "height": 44
                    },
                    {
                        "id": "field-1",
                        "node": "779:89896",
                        "label": "Hex",
                        "height": 22,
                        "icon": "panel-color-hex"
                    },
                    {
                        "id": "field-2",
                        "node": "779:89900",
                        "label": "Opacity",
                        "height": 52,
                        "icon": "brush-opacity"
                    },
                    {
                        "id": "field-3",
                        "node": "779:89907",
                        "label": "Blend",
                        "height": 54,
                        "icon": "panel-blend-mode",
                        "options": [
                            "Normal",
                            "Multiply",
                            "Screen"
                        ],
                        "desktopColumns": 3
                    }
                ],
                "mode": "Solid"
            },
            {
                "id": "gradient",
                "title": "Gradient",
                "icon": "panel-fill-gradient",
                "node": "779:89923",
                "height": 446,
                "fields": [
                    {
                        "id": "field-4",
                        "node": "779:90020",
                        "label": "Type",
                        "height": 54,
                        "icon": "panel-gradient-type",
                        "options": [
                            "Linear",
                            "Radial",
                            "Angular"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-5",
                        "node": "779:90031",
                        "label": "Gradient stops",
                        "height": 44
                    },
                    {
                        "id": "field-6",
                        "node": "779:90033",
                        "label": "Angle",
                        "height": 52,
                        "icon": "rotate"
                    },
                    {
                        "id": "field-7",
                        "node": "779:90040",
                        "label": "Dither",
                        "height": 22,
                        "icon": "panel-gradient-dither"
                    },
                    {
                        "id": "field-2",
                        "node": "779:90046",
                        "label": "Opacity",
                        "height": 52,
                        "icon": "brush-opacity"
                    },
                    {
                        "id": "field-3",
                        "node": "779:90053",
                        "label": "Blend",
                        "height": 54,
                        "icon": "panel-blend-mode",
                        "options": [
                            "Normal",
                            "Multiply",
                            "Screen"
                        ],
                        "desktopColumns": 3
                    }
                ],
                "mode": "Gradient"
            },
            {
                "id": "pattern",
                "title": "Pattern",
                "icon": "panel-fill-pattern",
                "node": "779:90069",
                "height": 444,
                "fields": [
                    {
                        "id": "field-8",
                        "node": "779:90166",
                        "label": "Pattern library",
                        "height": 44
                    },
                    {
                        "id": "field-9",
                        "node": "779:90168",
                        "label": "Scale",
                        "height": 52,
                        "icon": "brush-size"
                    },
                    {
                        "id": "field-10",
                        "node": "779:90175",
                        "label": "Rotation",
                        "height": 52,
                        "icon": "rotate"
                    },
                    {
                        "id": "field-11",
                        "node": "779:90182",
                        "label": "Seamless",
                        "height": 22,
                        "icon": "panel-pattern-seamless"
                    },
                    {
                        "id": "field-2",
                        "node": "779:90188",
                        "label": "Opacity",
                        "height": 52,
                        "icon": "brush-opacity"
                    },
                    {
                        "id": "field-3",
                        "node": "779:90195",
                        "label": "Blend",
                        "height": 54,
                        "icon": "panel-blend-mode",
                        "options": [
                            "Normal",
                            "Multiply",
                            "Screen"
                        ],
                        "desktopColumns": 3
                    }
                ],
                "mode": "Pattern"
            },
            {
                "id": "generative",
                "title": "Generative",
                "icon": "panel-fill-generative",
                "node": "779:90211",
                "height": 392,
                "fields": [
                    {
                        "id": "field-12",
                        "node": "779:90309",
                        "label": "Prompt",
                        "height": 22,
                        "icon": "panel-prompt-text"
                    },
                    {
                        "id": "field-13",
                        "node": "779:90313",
                        "label": "Variation",
                        "height": 52,
                        "icon": "panel-variation-grid"
                    },
                    {
                        "id": "field-14",
                        "node": "779:90320",
                        "label": "Match lighting",
                        "height": 22,
                        "icon": "panel-lighting-match"
                    },
                    {
                        "id": "field-2",
                        "node": "779:90326",
                        "label": "Opacity",
                        "height": 52,
                        "icon": "brush-opacity"
                    },
                    {
                        "id": "field-3",
                        "node": "779:90333",
                        "label": "Blend",
                        "height": 54,
                        "icon": "panel-blend-mode",
                        "options": [
                            "Normal",
                            "Multiply",
                            "Screen"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-15",
                        "node": "779:90344",
                        "label": "Generate fill",
                        "height": 22,
                        "icon": "panel-fill-generative"
                    }
                ],
                "mode": "Generative"
            }
        ]
    },
    "brush": {
        "node": "779:90746",
        "views": [
            {
                "id": "library",
                "title": "Library",
                "icon": "panel-brush-library",
                "node": "779:91722",
                "height": 286,
                "fields": [
                    {
                        "id": "field-0",
                        "node": "779:91854",
                        "label": "Search",
                        "height": 22,
                        "icon": "search-assets",
                        "placeholder": "Search brushes…"
                    },
                    {
                        "id": "field-1",
                        "node": "779:91858",
                        "label": "Category",
                        "height": 54,
                        "icon": "panel-asset-types",
                        "options": [
                            "Paint",
                            "Ink",
                            "Texture"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-2",
                        "node": "779:91869",
                        "label": "Brush presets",
                        "height": 44
                    },
                    {
                        "id": "field-3",
                        "node": "779:91871",
                        "label": "Manage library",
                        "height": 22,
                        "icon": "panel-brush-library"
                    }
                ]
            },
            {
                "id": "tip",
                "title": "Tip",
                "icon": "panel-brush-tip",
                "node": "779:91882",
                "height": 324,
                "fields": [
                    {
                        "id": "field-4",
                        "node": "779:92014",
                        "label": "Size",
                        "height": 52,
                        "icon": "brush-size"
                    },
                    {
                        "id": "field-5",
                        "node": "779:92021",
                        "label": "Hardness",
                        "height": 52,
                        "icon": "brush-hardness"
                    },
                    {
                        "id": "field-6",
                        "node": "779:92028",
                        "label": "Relative to zoom",
                        "height": 22,
                        "icon": "panel-zoom-relative"
                    },
                    {
                        "id": "field-7",
                        "node": "779:92034",
                        "label": "Shape",
                        "height": 54,
                        "icon": "panel-tip-shape",
                        "options": [
                            "Round",
                            "Flat",
                            "Custom"
                        ],
                        "desktopColumns": 3,
                        "disabledOptions": [
                            "Flat",
                            "Custom"
                        ]
                    }
                ]
            },
            {
                "id": "paint",
                "title": "Paint",
                "icon": "panel-brush-paint",
                "node": "779:92053",
                "height": 324,
                "fields": [
                    {
                        "id": "field-8",
                        "node": "779:92184",
                        "label": "Opacity",
                        "height": 52,
                        "icon": "brush-opacity"
                    },
                    {
                        "id": "field-9",
                        "node": "779:92191",
                        "label": "Flow",
                        "height": 52,
                        "icon": "brush-flow"
                    },
                    {
                        "id": "field-10",
                        "node": "779:92198",
                        "label": "Build-up",
                        "height": 22,
                        "icon": "panel-paint-build-up"
                    },
                    {
                        "id": "field-11",
                        "node": "779:92204",
                        "label": "Blend",
                        "height": 54,
                        "icon": "panel-blend-mode",
                        "options": [
                            "Normal",
                            "Multiply",
                            "Color",
                            "Overlay",
                            "Soft light"
                        ],
                        "desktopColumns": 5,
                        "disabledOptions": [
                            "Color",
                            "Soft light"
                        ]
                    }
                ]
            },
            {
                "id": "pressure",
                "title": "Pressure",
                "icon": "panel-brush-pressure",
                "node": "779:92227",
                "height": 322,
                "fields": [
                    {
                        "id": "field-12",
                        "node": "779:92359",
                        "label": "Pressure → Size",
                        "height": 52,
                        "icon": "panel-pressure-size"
                    },
                    {
                        "id": "field-13",
                        "node": "779:92366",
                        "label": "Pressure → Opacity",
                        "height": 52,
                        "icon": "panel-pressure-opacity"
                    },
                    {
                        "id": "field-14",
                        "node": "779:92373",
                        "label": "Tilt → Angle",
                        "height": 52,
                        "icon": "panel-tilt-angle"
                    },
                    {
                        "id": "field-15",
                        "node": "779:92380",
                        "label": "Invert pressure",
                        "height": 22,
                        "icon": "panel-invert-pressure"
                    }
                ]
            },
            {
                "id": "stroke",
                "title": "Stroke",
                "icon": "panel-brush-stroke",
                "node": "779:92394",
                "height": 322,
                "fields": [
                    {
                        "id": "field-16",
                        "node": "779:92525",
                        "label": "Spacing",
                        "height": 52,
                        "icon": "panel-stroke-spacing"
                    },
                    {
                        "id": "field-17",
                        "node": "779:92532",
                        "label": "Smoothing",
                        "height": 52,
                        "icon": "brush-smoothing"
                    },
                    {
                        "id": "field-18",
                        "node": "779:92539",
                        "label": "Streamline",
                        "height": 52,
                        "icon": "panel-stroke-streamline"
                    },
                    {
                        "id": "field-19",
                        "node": "779:92546",
                        "label": "Catch-up",
                        "height": 22,
                        "icon": "panel-stroke-catch-up"
                    }
                ]
            },
            {
                "id": "color",
                "title": "Color",
                "icon": "panel-brush-color",
                "node": "779:92560",
                "height": 280,
                "fields": [
                    {
                        "id": "field-20",
                        "node": "779:92693",
                        "label": "Foreground / mix",
                        "height": 44
                    },
                    {
                        "id": "field-21",
                        "node": "779:92695",
                        "label": "Wet mix",
                        "height": 52,
                        "icon": "panel-wet-mix"
                    },
                    {
                        "id": "field-22",
                        "node": "779:92702",
                        "label": "Color dynamics",
                        "height": 52,
                        "icon": "panel-color-dynamics"
                    }
                ]
            },
            {
                "id": "stabilizer",
                "title": "Stabilizer",
                "icon": "panel-brush-stabilizer",
                "node": "779:92717",
                "height": 324,
                "fields": [
                    {
                        "id": "field-23",
                        "node": "779:92848",
                        "label": "Mode",
                        "height": 54,
                        "icon": "panel-stabilizer-mode",
                        "options": [
                            "Rope",
                            "Predictive",
                            "Pulled"
                        ],
                        "desktopColumns": 3
                    },
                    {
                        "id": "field-24",
                        "node": "779:92859",
                        "label": "Strength",
                        "height": 52,
                        "icon": "panel-stabilizer-strength"
                    },
                    {
                        "id": "field-25",
                        "node": "779:92866",
                        "label": "Delay",
                        "height": 52,
                        "icon": "panel-delay-clock"
                    },
                    {
                        "id": "field-26",
                        "node": "779:92873",
                        "label": "Angle snapping",
                        "height": 22,
                        "icon": "panel-angle-snap"
                    }
                ]
            }
        ]
    }
}

function tool(key) { return tools[key] || null }
function views(key) { return tool(key) ? tool(key).views : [] }
function view(key, id) { return views(key).find(function(entry) { return entry.id === id }) || null }
