# AnybandUI

A native, customizable interface for Angband 4.2.6.

## AnybandUI development build

This fork now includes an experimental semantic gameplay backend and a native
C++ SDL3/SDL_GPU client. The adapter uses existing engine hooks; gameplay
source files are unchanged. The design is
documented in the [AnybandUI specification](docs/anybandui-specification.md) and
[proposed API contract](docs/anybandui-api.md). The current implementation speaks
development protocol 0.1, not the proposed stable v1 contract.

See [building and using AnybandUI](anybandui/README.md) for launch instructions,
implemented features and remaining work. Windows has been exercised locally;
macOS and Linux validation is still outstanding.

<p align="center">
  <img src="screenshots/title.png" width="425"/>
  <img src="screenshots/game.png" width="425"/>
</p>

Angband is a graphical dungeon adventure game that uses textual characters to
represent the walls and floors of a dungeon and the inhabitants therein, in the
vein of games like NetHack and Rogue. If you need help in-game, press `?`.

- **Installing Angband:** See the [Official Website](https://angband.github.io/angband/) or [compile it yourself](https://angband.readthedocs.io/en/latest/hacking/compiling.html).
- **How to Play:** [The Angband Manual](https://angband.readthedocs.io/en/latest/)
- **Getting Help:** [Angband Forums](https://angband.live/forums/)

Enjoy!

-- The Angband Dev Team
