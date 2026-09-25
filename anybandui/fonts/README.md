# Bundled fonts

The fourteen supplied TTF files were copied unchanged from the user-provided
fonts folder. Cousine Regular was copied unchanged from the pinned Dear ImGui
checkout and is maintained here as the missing-glyph fallback, rather than loaded
from a dependency's source directory. Nouveau IBM is the default interface font;
the dungeon inherits the interface font until separately configured.

`metadata.json` preserves embedded family, designer, copyright and licence
metadata (TrueType name IDs 0, 1, 2, 8, 9, 13 and 14). File names remain stable
settings IDs. Cousine's OFL licence and copyright notice are also included under
`anybandui/licenses` and in Windows packages. User-provided fonts were supplied as
freely licensed assets; embedded notices remain in the original files.

The catalog and display labels live in `font_library.h`. Each non-default face
merges Cousine as a missing-glyph fallback. Faces are loaded once; selections do
not destroy the atlas while GPU draw data references it. Dungeon cells use the
largest printable ASCII advance so decorative fonts retain a regular tile grid.

Tengwar Annatar by Johan Winge is bundled unchanged in `tengwar-annatar/`,
including all six original distribution files and its original licence. The
regular face is used only for cursed unique-enemy effects, not the UI picker.
