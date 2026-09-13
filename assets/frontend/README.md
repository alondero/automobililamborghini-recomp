# Frontend assets

These are UI-only assets; no game/ROM assets are included.

Generic SVG icons and the Noto Emoji / promptfont font binaries were obtained from
Zelda64Recomp revision `1a9c26613c6e0906140dc8bcca7362cbe00bf1eb`, `assets/`:
https://github.com/Zelda64Recomp/Zelda64Recomp/tree/1a9c26613c6e0906140dc8bcca7362cbe00bf1eb/assets

The source project's generic UI artwork is GPL-3.0, matching this repository's LICENSE.
Keyboard, Cont, PlusKeyboard, RecordSpinner, Caret and Question SVGs are simple original
icons added for the frontend experiment. `recomp.rcss` selects the shared frontend font;
the component styling comes from RecompFrontend itself.

Fonts retain their SIL Open Font License: see `NotoEmoji-LICENSE.txt`,
`promptfont/LICENSE.txt`, and `Lato-LICENSE.txt`. The Noto license is published at
https://github.com/googlefonts/noto-emoji/blob/main/fonts/LICENSE . LatoLatin regular/bold
are copied at build time from the pinned RmlUi samples and are not duplicated here.
