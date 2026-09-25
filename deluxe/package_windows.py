"""Create a relocatable Windows playtest ZIP; never includes personal saves."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def package(build, output):
    build, output = build.resolve(), output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = "AnybandUI-Windows-" + datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    stage = output / name
    stage.mkdir()  # Never overwrite a previous package or someone else's files.
    game = build / "game"
    for file in ("AnybandUI.exe", "angband-backend.exe"):
        shutil.copy2(game/file, stage/file)
    for folder in ("audio", "fonts"):
        shutil.copytree(game/folder, stage/folder)
    for folder in ("gamedata", "customize", "help", "screens", "tiles"):
        shutil.copytree(ROOT/"lib"/folder, stage/"data"/folder,
                        ignore=shutil.ignore_patterns("Makefile*", "*.am", "*.in"))
    # App-local release runtimes: a clean PC does not need Visual Studio.
    candidates = sorted(Path("C:/Program Files/Microsoft Visual Studio").glob(
        "*/*/VC/Redist/MSVC/[0-9]*/x64/Microsoft.VC*.CRT"))
    if not candidates:
        raise RuntimeError("Install the Visual C++ x64 redistributable build tools before packaging")
    for file in candidates[-1].glob("*.dll"):
        shutil.copy2(file, stage/file.name)
    licenses = stage/"licenses"
    shutil.copytree(ROOT/"deluxe"/"licenses", licenses)
    deps = ROOT/"build-deluxe"/"_deps"
    for source, license_name in (("sdl3-src/LICENSE.txt", "SDL3.txt"),
                         ("imgui-src/LICENSE.txt", "Dear-ImGui.txt"),
                         ("json-src/LICENSE.MIT", "nlohmann-json.txt"),
                         ("cjson-src/LICENSE", "cJSON.txt")):
        shutil.copy2(deps/source, licenses/license_name)
    shutil.copy2(ROOT/"docs"/"copying.rst", licenses/"Angband-copying.rst")
    shutil.copy2(deps/"sdl3-src"/"src"/"video"/"stb_image.h", licenses/"stb_image.h")
    (licenses/"Cousine-copyright.txt").write_text(
        "Cousine-Regular.ttf by Steve Matteson. Digitized data copyright (c) 2010 Google Corporation.\n"
        "Licensed under the SIL Open Font License 1.1; see Cousine-OFL.txt.\n")
    shutil.copy2(ROOT/"deluxe"/"PLAYTEST.md", stage/"START-HERE.md")
    # Bundle exact working-tree source alongside binaries, including local fixes.
    tracked = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT).decode().split("\0")
    with zipfile.ZipFile(stage/"source.zip", "w", zipfile.ZIP_DEFLATED) as source:
        for file in sorted(set(tracked)):
            path = ROOT/file
            if file and path.is_file() and not path.is_relative_to(stage) and not file.endswith(".pyc") and not file.startswith("screenshots/"):
                source.write(path, "AnybandUI-source/"+file)
    manifest = {}
    for path in sorted(stage.rglob("*")):
        if path.is_file():
            manifest[path.relative_to(stage).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    (stage/"manifest.json").write_text(json.dumps({"sha256":manifest}, indent=2))
    archive = Path(shutil.make_archive(str(output/name), "zip", output, name))
    print(archive)
    return archive, stage

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT/"build-deluxe-native")
    parser.add_argument("--output", type=Path, default=ROOT/"build-deluxe"/"packages")
    args = parser.parse_args()
    package(args.build, args.output)
