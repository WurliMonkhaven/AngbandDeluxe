"""Repeatable Windows release checks, isolated journeys and performance baselines."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import unittest
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--package", action="store_true")
    parser.add_argument("--ui-fixture", type=Path, help="Captured UI fixture for offscreen layout and inventory interaction checks")
    args = parser.parse_args()
    build = ROOT/"build-deluxe-native"
    game = build/"game"
    output = ROOT/"build-deluxe"/("readiness-"+datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S"))
    output.mkdir()
    records = []
    def run(name, command, cwd=ROOT, timeout=240, expected=0):
        print(name, flush=True)
        start = time.monotonic()
        with (output/(name+".log")).open("w",encoding="utf-8") as log:
            result = subprocess.run([str(x) for x in command], cwd=cwd, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        records.append({"check":name,"seconds":round(time.monotonic()-start,3),"exit_code":result.returncode})
        (output/"results.json").write_text(json.dumps(records,indent=2))
        if result.returncode != expected:
            raise RuntimeError(f"{name} failed; see {output / (name+'.log')}")
    python = [sys.executable,"-B"]
    if not args.skip_build:
        run("build",python+[ROOT/"deluxe/build.py","--ninja","--target","OurExecutable","angband-deluxe",
                            "deluxe-client-tests","deluxe-gpu-tests","deluxe-audio-tests","deluxe-transport-tests","deluxe-ui-preview","deluxe-window-tests"],timeout=600)
    run("client",[game/"deluxe-client-tests.exe",output/"settings.json"])
    run("backend",python+[ROOT/"deluxe/test_backend.py","--backend",game/"angband-backend.exe"])
    run("audio",[game/"deluxe-audio-tests.exe",game/"audio"])
    run("gpu",[game/"deluxe-gpu-tests.exe","direct3d12"])
    run("frame-pacing",[game/"deluxe-gpu-tests.exe","direct3d12","--bench"])
    run("assets",[game/"angband-deluxe.exe","--check-assets"])
    # Create a fresh first-floor fixture through actual game commands, never a
    # personal save. The fixture remains for diagnosis.
    import test_backend as engine_tests
    engine_tests.ARGS=SimpleNamespace(backend=game/"angband-backend.exe",data=ROOT/"lib",verbose=False)
    fixture=output/"transport-profile"
    class Journey(engine_tests.BackendTests):
        captured=False
        def assert_semantic_view(self,state):
            super().assert_semantic_view(state)
            if not self.captured and state["player"]["depth"]==1:
                self.captured=True
                (output/"dungeon-ui.json").write_text(json.dumps({"state":state}),encoding="utf-8")
                self.engine.call("session.save")
                shutil.copytree(Path(self.temp.name)/"save",fixture/"save")
    with (output/"fixture.log").open("w") as log:
        result=unittest.TextTestRunner(stream=log).run(Journey("test_stairs_and_dungeon_inspection"))
    if not result.wasSuccessful() or not (fixture/"save/ProtocolTest").exists():
        raise RuntimeError("Dungeon benchmark fixture failed; see fixture.log")
    run("detached-windows",[game/"deluxe-window-tests.exe",output/"dungeon-ui.json"])
    if args.ui_fixture:
        for kind in ("layout", "inventory"):
            run(kind+"-interaction",python+[ROOT/f"deluxe/test_{kind}_ui.py","--preview",game/"deluxe-ui-preview.exe",
                "--fixture",args.ui_fixture.resolve(),"--output",output/(kind+"-interaction")])
    # Keep the full generated dungeon, but remove nearby combat interference
    # from the movement timing fixture through the existing wizard command.
    probe=engine_tests.Engine(fixture)
    def settle():
        for _ in range(30):
            probe.call("state.get")
            if probe.prompt:
                prompt=probe.prompt; probe.prompt=None
                if prompt["type"] not in ("confirmation","quantity"):
                    raise RuntimeError("Unexpected fixture prompt: "+str(prompt))
                old=probe.state["revision"]
                result=probe.call("prompt.reply",{"prompt_id":prompt["prompt_id"],"value":True if prompt["type"]=="confirmation" else prompt["maximum"]})
                if "result" not in result: raise RuntimeError(str(result))
                probe.next_state(old)
            elif probe.state.get("readiness")=="ready": return
            else: probe.key("enter")
        raise RuntimeError("Benchmark fixture did not settle")
    try:
        probe.hello(); probe.call("session.load",{"save":"ProtocolTest"}); probe.next_state(None); settle()
        for key in ("z","d"):
            probe.key(1); probe.key(ord(key)); settle()
        probe.call("session.close"); probe.process.wait(timeout=10)
    finally:
        probe.stop()
    run("transport",[game/"deluxe-transport-tests.exe",game/"angband-backend.exe",ROOT/"lib",fixture])
    if args.package:
        from package_windows import package
        archive,stage=package(build,output/"packages")
        # Relocate to a path with spaces; cwd deliberately unrelated to the app.
        relocated=output/"Relocated build with spaces"
        shutil.copytree(stage,relocated)
        run("packaged-assets",[relocated/"angband-deluxe.exe","--check-assets"],cwd=output)
        asset_log=(output/"packaged-assets.log").read_text()
        resolved=[Path(line.split("=",1)[1]) for line in asset_log.splitlines() if "=" in line]
        if len(resolved)!=4 or not all(path.is_relative_to(relocated) for path in resolved):
            raise RuntimeError("Packaged client fell back to source-tree assets")
        import hashlib
        manifest=json.loads((relocated/"manifest.json").read_text())["sha256"]
        for relative,digest in manifest.items():
            if hashlib.sha256((relocated/relative).read_bytes()).hexdigest()!=digest:
                raise RuntimeError("Package checksum failed: "+relative)
        font=relocated/"fonts/Cousine-Regular.ttf"
        backup=font.with_suffix(".test-backup")
        font.rename(backup)
        try:
            run("packaged-missing-font",[relocated/"angband-deluxe.exe","--check-assets"],cwd=output,expected=1)
        finally:
            backup.rename(font)
        run("packaged-journeys",python+[ROOT/"deluxe/test_backend.py","--backend",relocated/"angband-backend.exe",
            "--data",relocated/"data","BackendTests.test_native_birth","BackendTests.test_native_birth_cancel",
            "BackendTests.test_run_summary_and_replay","BackendTests.test_stairs_and_dungeon_inspection"],cwd=output)
        run("packaged-audio",[game/"deluxe-audio-tests.exe",relocated/"audio"],cwd=output)
        (output/"package.txt").write_text(str(archive)+"\n")
    print(f"All checks passed. Results: {output}")

if __name__=="__main__":
    main()
