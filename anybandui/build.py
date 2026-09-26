"""Configure/build the native applications; Python is only a build helper."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--configure", action="store_true")
parser.add_argument("--config", default="RelWithDebInfo")
parser.add_argument("--target", nargs="+", default=["AnybandUI"])
parser.add_argument("--ninja", action="store_true")
args = parser.parse_args()
cmake = shutil.which("cmake")
if not cmake and os.name == "nt":
    candidates = list(Path("C:/Program Files/Microsoft Visual Studio").glob(
        "*/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"))
    if candidates:
        cmake = str(sorted(candidates)[-1])
if not cmake:
    raise SystemExit("Install CMake and a native C/C++ compiler first.")
# Some Windows hosts supply both Path and PATH; MSBuild rejects that environment.
env = {(k.upper() if os.name == "nt" else k): v for k, v in os.environ.items()}
if os.name == "nt" and "PATH" in env:
    env["Path"] = env.pop("PATH")
build = root / ("build-ui-native" if args.ninja else "build-ui")
extra = ["-G", "Ninja", f"-DCMAKE_BUILD_TYPE={args.config}"] if args.ninja else []
if args.ninja and os.name == "nt":
    vs = Path(cmake).parents[7]
    vcvars = vs / "VC/Auxiliary/Build/vcvars64.bat"
    configured = subprocess.check_output(
        f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',
        env=env, text=True)
    env = {k.upper(): v for line in configured.splitlines() if "=" in line
           for k, v in [line.split("=", 1)]}
    ninja = vs / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
    extra.append(f"-DCMAKE_MAKE_PROGRAM={ninja}")
    for dep in ("sdl3", "imgui", "json"):
        source = root / "build-anybandui/_deps" / f"{dep}-src"
        if source.exists():
            extra.append(f"-DFETCHCONTENT_SOURCE_DIR_{dep.upper()}={source}")
cache = build / "CMakeCache.txt"
config_changed = args.ninja and cache.exists() and f"CMAKE_BUILD_TYPE:STRING={args.config}\n" not in cache.read_text()
if args.configure or not cache.exists() or config_changed:
    subprocess.run([cmake, "-S", str(root), "-B", str(build),
        *extra], env=env, check=True)
subprocess.run([cmake, "--build", str(build), "--config", args.config,
                "--target", *args.target, "--parallel", "8"], env=env, check=True)
