"""Keep editor button roles independent from interaction and selection state."""

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
EDITOR = ROOT / "src/App/Views/Editor"


class EditorButtonStateContract(unittest.TestCase):
    def test_button_tones_are_fixed_roles(self):
        # The first generic action has a stable primary role. Its index never
        # represents hover, press, focus or selection.
        role = "actionSlot.index === 0 ? LV.AbstractButton.Primary : LV.AbstractButton.Default"
        # ComboBox has its own Primary/Borderless enum. These are also fixed
        # roles; AbstractButton.Borderless has a different numeric value.
        literal = re.compile(r"LV\.(?:AbstractButton\.(?:Primary|Default|Borderless|Destructive|Disabled)|ComboBox\.(?:Primary|Borderless))$")
        failures = []
        for path in sorted(EDITOR.glob("*.qml")):
            for number, line in enumerate(path.read_text().splitlines(), 1):
                for match in re.finditer(r"\btone\s*:\s*([^;\n}]+)", line):
                    expression = match.group(1).strip()
                    if not literal.fullmatch(expression) and expression != role:
                        failures.append(f"{path.name}:{number}: {expression}")
                if re.search(r"\btone\s*=(?!=)", line):
                    failures.append(f"{path.name}:{number}: imperative tone assignment")
        self.assertEqual(failures, [], "Button Kind must stay fixed; use interaction/checked state:\n" + "\n".join(failures))


if __name__ == "__main__":
    unittest.main()
