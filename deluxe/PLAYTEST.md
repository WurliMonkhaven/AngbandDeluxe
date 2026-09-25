# AnybandUI — Windows playtest

Extract the entire ZIP to a folder, then open **AnybandUI.exe**.
Do not launch it from inside the ZIP. No Python, Visual Studio or installer is
needed. This build targets 64-bit Windows with a GPU supported by SDL3.

Choose **New character**, or choose an existing character. The game follows
Angband's rules: death ends that character's adventure. Opening a dead save
lets you start again with the same starting build or create someone different.

- Move with the numpad or click a destination. Right-click for tile actions.
- The message ribbon means the game is waiting for acknowledgement.
- Use the right-hand Inventory, Spells and other tabs for native controls.
- **Save and…** offers save, return to menu, and quit choices.
- **Settings** controls gameplay, keybindings, animations, audio and CRT effects.
- Start with CRT Off if you want to compare responsiveness.

Existing installations retain their original save/settings folder automatically.
Fresh installations store saves and settings in `%APPDATA%/AnybandUI/AnybandUI`, independently
of this extracted folder. Replacing or deleting the extracted build does not
remove them. Back up that user folder before trying an experimental build.
To keep a completely separate playtest profile, launch from PowerShell:

```powershell
.\AnybandUI.exe --user-dir "C:\AngbandPlaytest"
```

## What to try

Create a character, explore the first dungeon floor, visit a shop, save and
reload. Try cancelling item selection, casting without sufficient mana, and
resizing the window with a message waiting. If the character dies, try Play
Again and cancelling back to the menu. Check CRT Off and your preferred preset.

Please report the steps, expected behaviour, actual behaviour, and whether CRT
was enabled. Include a screenshot or a copy of the affected save when useful.

## Build contents

`data`, `fonts` and `audio` must stay next to the executables. `licenses` contains
third-party notices. `source.zip` contains the corresponding source and build
instructions. `manifest.json` lists SHA-256 hashes of the package contents.
Microsoft Visual C++ release runtime DLLs are included beside the application.
This is an unsigned playtest build, not a signed installer. macOS/Linux and
clean-machine installation have not yet been manually certified.
