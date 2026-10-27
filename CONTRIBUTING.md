# Contributing

## The one hard rule: no game material

vita-runtime must stay free of anything taken from a commercial game. Do not
contribute:

- Decompiled or disassembled code, symbol names, or addresses from any game
- Game assets, archives, save files, or samples of proprietary file formats
- Crash dumps, logs or screenshots that contain game data
- Names, types or constants that only make sense for one particular game

Pull requests that include any of this will be closed and the history
rewritten to remove it.

## Design rules

- The public API lives in `include/vr/` and is plain C99.
- Describe features as generic capabilities ("DXT5 textures with mip
  chains"), never as "what game X needs".
- Every feature comes with a test in `tests/` or a sample that exercises it,
  using only original content.
- Backends share behavior: anything implemented for `vita` should have a
  host counterpart, even if it is a stub.

## Workflow

1. Open or pick an issue.
2. Build and run the tests on the host backend (see README).
3. Open a pull request. CI must be green.
