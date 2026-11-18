from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class GraphicHelperCTestContract(unittest.TestCase):
    def test_canvas_and_graphic_adjust_package_versions(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertRegex(cmake, r'find_package\(iiSharedCanvas\s+0\.35\.39\s+EXACT\s+CONFIG\s+REQUIRED\s+COMPONENTS\s+Quick\b')
        self.assertRegex(cmake, r'find_package\(iiGraphicAdjust\s+0\.6\.2\s+EXACT\s+CONFIG\s+REQUIRED\b')
        review = (ROOT / 'cmake/NativeReviewNavigation.cmake').read_text()
        self.assertRegex(review, r'find_package\(iiSharedCanvasNative\s+0\.35\.39\s+CONFIG\s+REQUIRED\b')
        app = (ROOT / 'cmake/NativeViewApp.cmake').read_text()
        self.assertRegex(app, r'find_package\(iiGraphicAdjust\s+0\.6\.2\s+CONFIG\s+REQUIRED\b')
        standalone = (ROOT / 'tests/graphic_helpers/CMakeLists.txt').read_text()
        self.assertRegex(standalone, r'find_package\(iiGraphicAdjust\s+0\.6\.2\s+EXACT\s+CONFIG\s+REQUIRED\b')

    def test_cases_rows_and_assertions_are_preserved(self):
        text = (ROOT / 'tests/tst_graphichelpervalues.cpp').read_text()
        self.assertNotRegex(text, r'\bQTest\b|\bQTEST_|<QtTest>|\bQ(?:VERIFY|COMPARE|FETCH)\w*')
        for case in ('everyExistingHelperRetainsItsSettings', 'pureHelpersNeedNoRasterInput',
                     'imageHelpersRejectMissingInput', 'malformedParametersAreRejected',
                     'cancellationAndSdkValidationReturnNoParameters', 'catalogAndCanonicalNamesAreComplete'):
            self.assertIn(case, text)
        for row in ('light', 'white', 'sample-white', 'preset', 'mix', 'point', 'lens-scale', 'chromatic',
                    'not-helper', 'unknown-light', 'unknown-wb', 'no-sample', 'invalid-sample',
                    'boolean-temperature', 'nan', 'extra-mix', 'extra-ca'):
            self.assertIn('"' + row + '"', text)
        self.assertIn('GA::toArgb32(input.view())', text)
        self.assertIn('EditorGraphicAdjust::helperParameters(result)', text)

    def test_target_uses_ctest_without_qttest(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        match = re.search(r'target_link_libraries\(DreamscapesGraphicHelperValuesTests\s+([^)]*)\)', cmake)
        self.assertIsNotNone(match)
        self.assertNotIn('Qt6::Test', match.group(1))
        self.assertIn('Dreamscapes.GraphicHelperCTestContract', cmake)
        standalone = (ROOT / 'tests/graphic_helpers/CMakeLists.txt').read_text()
        self.assertNotIn('Qt6::Test', standalone)
        self.assertNotRegex(standalone, r'find_package\(Qt6[^)]*\bTest\b')
        self.assertIn('Dreamscapes.GraphicHelperValuesInvalidParameters', standalone)

if __name__ == '__main__':
    unittest.main()
