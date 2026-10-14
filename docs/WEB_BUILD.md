# Browser test build (iPhone)

A WebAssembly build of the same game, for quick checks on a phone without the Android device. Only Tee can open it. Offline play only: Practice, the Sandbox and bot matches. Multiplayer networking (ENet/UDP) cannot run in a browser.

## Using it
1. Open the private link (Cloudflare Pages behind Cloudflare Access: sign in with Tee's email, one-time code).
2. First time only: tap **Choose oot.otr** and pick the file. It is stored in that browser (IndexedDB) and never uploaded. After that tap **Start**.
3. Touch controls: left stick = move, A/B/Z/R/Start on the right, C buttons as a diamond. They send the keyboard keys the game already maps (WASD, X, C, Z, R, Space, arrows).

The page ships no game data: `soh.otr` (our own assets, same file the Android app bundles) and the game binary only. `oot.otr` is never in git, CI or the host.

## Auto-update
`version.txt` (build number + commit) is fetched fresh on every open and added to every file URL, so a new build is never served from an old cache. While playing, the page checks every 2 minutes and shows "New build ... ready: tap to update"; the saved `oot.otr` survives the reload.

## How it is built
`scripts/web_build.sh` (Emscripten 6.0.12, deps from git into `~/.cache/oot-web`). It copies the repo, applies `patches/`, links the mod, compiles to `soh.js` + `soh.wasm`, then writes `web-dist/` with the wasm gzipped (63 MB raw, about 19 MB gzipped; hosts cap single files at 25 MB; the page unpacks it).
`.github/workflows/web-build.yml` runs it on the same pushes as the game build, caches ccache + deps, and publishes with wrangler when `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` secrets exist.

## What differs from the Android build (all behind `__EMSCRIPTEN__`)
- No threads: audio is made inline each frame, the resource/save thread pools run tasks on the caller (`WebThreadPool.hpp`), spdlog is synchronous, the Water texture generation runs inline, OoT-instrument music worker is skipped.
- The browser owns the frame loop (`emscripten_set_main_loop` in `Graph_ThreadEntry`; `RunFrame` already returns once per frame).
- GLES 3 / WebGL2 context. GLEW skipped.
- `func_800AA16C` call fixed to its real signature (WebAssembly traps on a mismatch; native builds hid it).

## Known limits
- Not yet checked on a real iPhone: speed, memory limits, touch feel, audio start (iOS needs a tap first; the Start button is that tap).
- Headless Chrome without a GPU runs it at a few frames per second; that only proves it boots and the menus work.
