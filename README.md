# AnybandUI

A standalone native frontend for engines implementing the
[AnybandUI full-v1 protocol](protocol/README.md). This repository contains no
Angband engine source, game data or bundled engine binary.

Build on Windows with Visual Studio C++ tools and Python:

```powershell
python -B anybandui/build.py --ninja
```

Run `build-ui-native/game/AnybandUI.exe`. With no engine installed it opens and
shows **No supported Anyband binaries found**. Put a supported engine package
under `build-ui-native/game/engines/` and click **Rescan**. Use the Engine picker
to choose between installed packages. Font/audio assets are staged automatically.

The reference engine is built separately from the `angband` repo's
`4.2.6-anybandui` branch. It is not a build dependency of this project. Package
installation copies compiled files and engine data, never engine source.

```powershell
python -B anybandui/readiness.py
python -B anybandui/package_windows.py
python -B protocol/check_engine.py PATH/engine.anyband.json
```

The frontend ZIP deliberately ships without an engine. UI settings and engine
saves are kept outside the installation directory. See [the protocol](protocol/README.md)
for package layout, version checks and save isolation. GPL-2.0; see LICENSE.
