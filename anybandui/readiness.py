"""Check the standalone frontend; optionally check an independently packaged engine."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--package", action="store_true")
    parser.add_argument("--engine", type=Path, help="Optional engine.anyband.json to validate")
    parser.add_argument("--ui-fixture", type=Path)
    args = parser.parse_args()
    build = ROOT/"build-ui-native"
    game = build/"game"
    output = build/("readiness-"+datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S"))
    output.mkdir(parents=True)
    python = [sys.executable, "-B"]
    records = []
    def run(name, command, cwd=ROOT, timeout=240):
        print(name, flush=True)
        with (output/(name+".log")).open("w", encoding="utf-8") as log:
            result = subprocess.run([str(x) for x in command], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        records.append({"check":name, "exit_code":result.returncode})
        (output/"results.json").write_text(json.dumps(records,indent=2))
        if result.returncode: raise RuntimeError(f"{name} failed; see {output}")
    if not args.skip_build:
        run("build",python+[ROOT/"anybandui/build.py","--ninja","--target","AnybandUI","anybandui-client-tests","anybandui-gpu-tests","anybandui-audio-tests","anybandui-ui-preview","anybandui-window-tests"],timeout=900)
    run("client",[game/"anybandui-client-tests.exe",output/"settings.json"])
    run("audio",[game/"anybandui-audio-tests.exe",game/"audio"])
    run("gpu",[game/"anybandui-gpu-tests.exe","direct3d12"])
    run("assets",[game/"AnybandUI.exe","--check-assets"])
    fixture=output/"no-engine.json"
    fixture.write_text(json.dumps({"no_engine":True,"state":{"messages":[],"phase":"launcher"}}))
    run("no-engine-preview",[game/"anybandui-ui-preview.exe",fixture,output/"no-engine.bmp",1200,800])
    if args.ui_fixture:
        run("detached-windows",[game/"anybandui-window-tests.exe",args.ui_fixture.resolve()])
    if args.engine:
        run("engine-contract",python+[ROOT/"protocol/check_engine.py",args.engine.resolve()])
    if args.package:
        from package_windows import package
        archive,stage=package(build,output/"packages")
        relocated=output/"Relocated build with spaces"
        shutil.copytree(stage,relocated)
        run("packaged-assets",[relocated/"AnybandUI.exe","--check-assets"],cwd=output)
    print(f"All checks passed: {output}")

if __name__ == "__main__": main()
