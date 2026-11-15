"""Python access to the Dreamscapes native view inventory, not product services."""
import json
from pathlib import Path


class ViewCatalog:
    def __init__(self, manifest=None):
        path = Path(manifest) if manifest else Path(__file__).resolve().parents[3] / "resources/migration/view-manifest.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        self._views = tuple(data["views"])

    def views(self):
        return tuple(dict(view) for view in self._views)

    def find(self, path):
        return next((dict(view) for view in self._views if path in (view["source"], view["replacement"])), None)
