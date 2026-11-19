"""Exercise the daemon reference protocol with a private same-user broker."""

import json
from pathlib import Path
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import zlib


IDENTIFIER = "d82fa87d-a754-44e4-a8af-fa60388d0abc"
SECTIONS = {
    "files": "Files",
    "asset-library": "Assets",
    "generation-history": "Generation History",
    "models": "Models",
}


def chunk(tag, payload):
    """Encodes a PNG chunk with its required checksum."""
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload))


PNG = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(b"\0\x20\x40\x80")) + chunk(b"IEND", b""))


class Client(socketserver.StreamRequestHandler):
    """Implements the daemon's framed, revision-scoped directory contract."""

    def send(self, value):
        body = json.dumps(value).encode()
        self.wfile.write(struct.pack(">I", len(body)) + body)

    def handle(self):
        root = self.server.root
        container = str(root)
        self.send({"type": "state", "state": {"selectedContainer": container}})
        directory = None
        while header := self.rfile.read(4):
            size = struct.unpack(">I", header)[0]
            request = json.loads(self.rfile.read(size))
            assert request["container"] == container
            operation = request["op"]
            reply = {"type": "result", "id": request["id"], "accepted": True}
            if operation == "directoryOpen":
                folder = request["path"]
                assert folder in SECTIONS.values()
                total = 66 if folder == "Files" else 1
                directory = {
                    "path": folder, "generation": f"gen-{request['id']}",
                    "revision": "revision-1", "status": "ready", "count": total,
                }
                # Ready can arrive before the command's loading reply.
                self.send({"type": "directoryChanged", "container": container, "directory": directory})
                reply["directory"] = {**directory, "status": "loading"}
            elif operation == "directoryPage":
                assert directory and request["generation"] == directory["generation"]
                assert request["revision"] == directory["revision"]
                if directory["path"] == "Assets":
                    self.send({"type": "state", "state": {"selectedContainer": "/changed"}})
                    return
                folder, total = directory["path"], directory["count"]
                offset, limit = request["offset"], request["limit"]
                assert 1 <= limit <= 64 and 0 <= offset < total
                if folder == "Files":
                    names = ["remote.png"] + [f"example-{index:02}.png" for index in range(65)]
                else:
                    names = ["tiny.safetensors" if folder == "Models" else "previous.png"]
                entries = [
                    {"name": name, "path": f"{folder}/{name}", "directory": False, "resident": folder != "Files",
                     "modified": "2026-11-18T12:00:00.000Z", "size": "68"}
                    for name in names[offset:offset + limit]
                ]
                reply.update(generation=directory["generation"], revision=directory["revision"],
                             offset=offset, count=total, entries=entries)
            elif operation == "directoryActivate":
                assert directory and request["generation"] == directory["generation"]
                assert request["revision"] == directory["revision"]
                assert request["path"] == "Files/remote.png"
                path = root / request["path"]
                path.write_bytes(PNG)
                # A resident activation can emit its event before acceptance.
                self.send({"type": "directoryActivated", "container": container,
                           "generation": directory["generation"], "id": request["id"],
                           "relative": request["path"], "path": str(path), "error": ""})
            else:
                raise AssertionError(operation)
            self.send(reply)


def main():
    """Keeps all fixture payloads below resources and removes them after CTest."""
    with tempfile.TemporaryDirectory(prefix=".dsd-", dir=sys.argv[2]) as temporary:
        broker = Path(temporary).resolve()
        root = broker / "Fixture.societycontainer"
        root.mkdir()
        for name in SECTIONS.values():
            (root / name).mkdir()
        (root / ".society-drive.json").write_text(json.dumps({
            "identifier": IDENTIFIER, "sections": [{"id": key, "path": name} for key, name in SECTIONS.items()],
        }))
        tensor = json.dumps({"weight": {"dtype": "F32", "shape": [1], "data_offsets": [0, 4]}}).encode()
        (root / "Models" / "tiny.safetensors").write_bytes(struct.pack("<Q", len(tensor)) + tensor + bytes(4))
        (root / "Generation History" / "previous.png").write_bytes(PNG)
        address = broker / "backend.sock"
        assert len(str(address).encode()) < 104
        with socketserver.UnixStreamServer(str(address), Client) as server:
            server.root = root
            address.chmod(0o600)
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                subprocess.run([sys.argv[1], str(broker), str(root)], timeout=30, check=True)
            finally:
                server.shutdown()
                worker.join()


if __name__ == "__main__":
    main()
