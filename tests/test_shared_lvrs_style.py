"""Keep product chrome on LVRS while preserving editable document colors."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'src/App/Views'


class SharedLVRSStyleTests(unittest.TestCase):
    def test_view_typography_uses_shared_roles(self):
        # Keep sizes and weights responsive to LVRS changes instead of
        # introducing a second scale for the application chrome.
        for path in ROOT.rglob('*.qml'):
            source = path.read_text(encoding='utf-8')
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertNotRegex(source, r'font\.pixelSize\s*:\s*(?:\d|LV\.Theme\.scaleMetric\(\s*\d)')
                self.assertNotRegex(source, r'font\.weight\s*:\s*Font\.')
                self.assertNotRegex(source, r'font\.styleName\s*:\s*"(?:Regular|Medium|SemiBold|Bold)"')

    def test_panel_and_timeline_use_semantic_roles(self):
        panel = (ROOT / 'PanelFrame.qml').read_text(encoding='utf-8')
        timeline = (ROOT / 'Editor/EditorTimeline.qml').read_text(encoding='utf-8')
        self.assertIn('panelBorderColor: Style.Tokens.color("Theme Colors/Panel/canvas")', panel)
        for token in ('Text/descriptionColor', 'Accent/Yellow/Default', 'Accent/Blue/Default'):
            self.assertIn('Style.Tokens.color("Theme Colors/' + token + '")', timeline)
        for stale in ('#0c0c0c', '#f0f000', '#8d8d8d', '#548af7'):
            self.assertNotIn(stale, (panel + timeline).lower())

    def test_chrome_does_not_reintroduce_product_palette(self):
        for name in ('Home/NewCanvasDialog.qml', 'Home/CanvasSizePreview.qml', 'Home/PublishedList.qml',
                     'Editor/EditorElementTransform.qml', 'Editor/EditorGraphicRow.qml',
                     'Editor/CanvasEditor.qml', 'Editor/EditorPageDeck.qml'):
            with self.subTest(path=name):
                self.assertNotRegex((ROOT / name).read_text(encoding='utf-8'), r'#[0-9a-fA-F]{6,8}\b')

    def test_playhead_uses_the_same_shared_color_in_both_timelines(self):
        marker = (ROOT / 'TimelinePlayhead.qml').read_text(encoding='utf-8')
        self.assertIn('fillColor: Style.Tokens.color("Theme Colors/Accent/Blue/Default")', marker)
        for name in ('Editor/EditorTimeline.qml', 'Video/VideoTimeline.qml'):
            self.assertIn('Frames.TimelinePlayhead', (ROOT / name).read_text(encoding='utf-8'))

    def test_timeline_controls_use_shared_icons(self):
        editor = (ROOT / 'Editor/EditorTimeline.qml').read_text(encoding='utf-8')
        video = (ROOT / 'Video/VideoTimeline.qml').read_text(encoding='utf-8')
        self.assertNotIn('Assets/timeline-', editor)
        self.assertNotIn('Qt.resolvedUrl("Assets/"+iconControl.asset', video)
        self.assertIn('LV.Theme.iconPath(iconControl.asset)', video)

    def test_native_overlays_receive_shared_ui_colors(self):
        editor = (ROOT / 'Editor/CanvasEditor.qml').read_text(encoding='utf-8')
        options = (ROOT / 'Editor/EditorViewOptionRow.qml').read_text(encoding='utf-8')
        picker = (ROOT / 'ColorPicker.qml').read_text(encoding='utf-8')
        background = (ROOT / 'Editor/EditorBackgroundColorPanel.qml').read_text(encoding='utf-8')
        for source in (editor, options):
            self.assertIn('accentColor: Style.Tokens.color("Theme Colors/Accent/Blue/Default")', source)
            self.assertIn('outlineColor: Style.Tokens.color("Theme Colors/Text/bodyColor")', source)
        for role in ('foreground: Style.Tokens.color("Theme Colors/Accent/Blue/Default")',
                     'surface: Style.Tokens.color("Theme Colors/Panel/surfaceSolid")',
                     'guide: Style.Tokens.color("Theme Colors/Text/descriptionColor")'):
            self.assertIn(role, editor)
        for source in (picker, background):
            self.assertIn('markerDark: Style.Tokens.color("Theme Colors/Panel/canvas")', source)
            self.assertIn('markerLight: Style.Tokens.color("Theme Colors/Text/textTokenBase")', source)

    def test_editor_icons_resolve_through_shared_library(self):
        resolver = (ROOT / 'Editor/LvrsEditorIcons.js').read_text(encoding='utf-8')
        for entry in ('"AlignBottom": "align-bottom"', '"Vibrance": "vibrance"',
                      '"Sharpen": "sharpen"', '"sort-assets": "sort-assets"'):
            self.assertIn(entry, resolver)
        self.assertNotIn('"Assets/" + group', resolver)
        self.assertNotIn('Assets/Text/', resolver)
        self.assertIn('fillColor: Style.Tokens.color("Theme Colors/Text/bodyColor")', (ROOT / 'Editor/EditorTextPanel.qml').read_text(encoding='utf-8'))
        for name in ('EditorTextPanel.qml', 'EditorLayersPanel.qml', 'EditorGraphicPanel.qml',
                     'EditorElementsPanel.qml', 'EditorCameraPhotoPanel.qml', 'EditorFilePanel.qml'):
            self.assertIn('LvrsIcons.resolve', (ROOT / 'Editor' / name).read_text(encoding='utf-8'))
        self.assertNotIn('Assets/Layers/', (ROOT / 'Editor/EditorLayersPanel.qml').read_text(encoding='utf-8'))

    def test_editor_toolbar_uses_shared_icon_roles(self):
        toolbar = (ROOT / 'Editor/EditorToolbar.qml').read_text(encoding='utf-8')
        roles = {'elements': 'rectangle', 'text': 'text', 'camera-photo': 'panel-camera-live',
                 'file': 'document-properties', 'background': 'background', 'audio-track': 'audio-track',
                 'canvas': 'canvas-size', 'generative': 'spark', 'layers': 'panel-layers-stack',
                 'select': 'direct-select', 'color': 'color', 'effects': 'effect-stack',
                 'retouch': 'panel-retouch-heal', 'fill': 'fill', 'brush': 'brush',
                 'auto-enhance': 'auto-tone', 'masking': 'layer-mask', 'eraser': 'eraser'}
        for key, icon in roles.items():
            with self.subTest(tool=key):
                self.assertIn('key: "' + key + '", icon: "' + icon + '"', toolbar)
        self.assertNotIn('Assets/tool-', toolbar)
        background = (ROOT / 'Editor/EditorBackgroundPanel.qml').read_text(encoding='utf-8')
        self.assertIn('LV.Theme.iconPath("panel-background-"', background)
        self.assertNotIn('Assets/Background/', background)
        self.assertIn('LV.Theme.iconPath("generalchevronRight")',
                      (ROOT / 'Editor/EditorToolControl.qml').read_text(encoding='utf-8'))

    def test_publishing_controls_do_not_use_product_color_assets(self):
        for name in ('PublishInspector.qml', 'PublishChoice.qml', 'PublishWorkspace.qml'):
            self.assertNotIn('Assets/', (ROOT / 'Publishing' / name).read_text(encoding='utf-8'))

    def test_home_controls_use_shared_icon_and_status_roles(self):
        quick = (ROOT / 'Home/QuickGenerate.qml').read_text(encoding='utf-8')
        self.assertIn('LV.Theme.iconPath("generalright")', quick)
        self.assertIn('LV.Theme.iconPath("generalchevronDown")', quick)
        self.assertIn('color: Style.Tokens.color("Theme Colors/Accent/Green/Default")', (ROOT / 'Home/HomeQueueBar.qml').read_text(encoding='utf-8'))

    def test_editable_color_data_is_not_rewritten_as_theme(self):
        palette = (ROOT / 'Editor/EditorBackgroundColorPanel.qml').read_text(encoding='utf-8')
        self.assertIn('"#8066FF"', palette)
        self.assertIn('backgroundGradientStops', palette)


if __name__ == '__main__':
    unittest.main()
