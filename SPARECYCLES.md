# Contributing with SpareCycles

vita-runtime is listed on a SpareCycles pool, where volunteers donate idle AI
compute. **Every SpareCycles prompt and answer is public**, and prompts are
run on volunteers' machines through their AI providers.

## Rules for jobs

- Write prompts against this repository's public code and docs only.
- Never paste in code, logs or data from a game or a private port, even
  "for context". If a bug only shows up in a game, reproduce it with a test
  or sample here first.
- Run AI coding tools pointed at the pool only from a clone of this
  repository. Tools like aider send files from the current directory.
- Never include secrets, keys or personal data.

## Writing a good job

Each job should be self-contained, since donor runners see only the prompt:

1. The goal, in one or two sentences.
2. The relevant public headers or file excerpts, pasted in.
3. Constraints: C99, no new dependencies, match the existing style.
4. What done looks like (for example, "a patch adding X plus a test in
   `tests/test_main.c`").

Review every answer as you would a pull request from a stranger: build it,
run the tests, and read the diff before merging.

## Starter jobs

- Vita backend for `vr_platform` (`sceKernelGetProcessTimeWide`,
  `sceKernelDelayThread`, logging to `ux0:data/<app>/log.txt`).
- Vita backend for `vr_fs` using `sceIo*`, rooted at a configurable
  `ux0:data/...` directory.
- Vita backend for `vr_input` using `SceCtrl` and `SceTouch`, with a
  configurable stick deadzone.
- SDL2 host backend for `vr_input`.
- VitaSDK build job in CI.
