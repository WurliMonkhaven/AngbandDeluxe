"""Check a packaged engine's full-v1 handshake and startup endpoints without game source."""
import argparse
import json
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time


def check(manifest):
    manifest = Path(manifest).resolve()
    package = json.loads(manifest.read_text(encoding="utf-8"))
    contract = json.loads(Path(__file__).with_name("full-v1.json").read_text())
    assert package["manifest_version"] == 1
    assert package["profile"] == contract["profile"]
    assert package["protocol"] == contract["protocol"]
    def child(key):
        relative = Path(package[key])
        assert not relative.is_absolute(), key
        path = (manifest.parent / relative).resolve()
        assert path.is_relative_to(manifest.parent), key
        assert path.exists(), str(path)
        return path
    with tempfile.TemporaryDirectory(prefix="anybandui-contract-") as profile:
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen([str(child("executable")), "--data-dir", str(child("data_directory")), "--user-dir", profile],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors, text=True, encoding="utf-8")
            incoming = queue.Queue()
            def read():
                try:
                    for line in process.stdout:
                        incoming.put(json.loads(line))
                except Exception as error:
                    incoming.put(error)
                finally:
                    incoming.put(EOFError("Engine closed its output"))
            threading.Thread(target=read, daemon=True).start()
            count = 0
            def call(method, params=None):
                nonlocal count
                count += 1
                rid = str(count)
                process.stdin.write(json.dumps({"kind":"request", "id":rid, "method":method, "params":params or {}}) + "\n")
                process.stdin.flush()
                deadline = time.monotonic() + 15
                while True:
                    item = incoming.get(timeout=max(.01, deadline-time.monotonic()))
                    if isinstance(item, Exception): raise item
                    if item.get("id") == rid:
                        assert "result" in item, item
                        return item["result"]
            try:
                hello = call("hello", {"protocols":[contract["protocol"]], "profile":contract["profile"],
                    "max_frame_bytes":contract["max_frame_bytes"], "native_inventory":True, "native_equipment":True})
                assert hello["protocol"] == contract["protocol"], hello
                assert hello["profile"] == contract["profile"], hello
                assert hello["max_frame_bytes"] >= contract["max_frame_bytes"]
                for key in ("id", "version", "save_compatibility"):
                    assert hello["engine"][key] == package["engine"][key], key
                for capability, version in contract["required_capabilities"].items():
                    assert hello["capabilities"].get(capability, 0) >= version, capability
                for method in ("saves.list", "commands.list", "catalog.get", "state.get", "tuning.get"):
                    call(method, {"session_id":"session-1"})
                process.stdin.close()  # Launcher has no active game to save.
                process.wait(timeout=10)
                assert process.returncode == 0, process.returncode
                print("PASS: full-v1 identity, capabilities, startup endpoints and clean shutdown")
            finally:
                if process.poll() is None: process.kill()
                process.wait()
                process.stdin.close()
                process.stdout.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    check(parser.parse_args().manifest)
