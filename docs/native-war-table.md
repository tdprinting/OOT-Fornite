# Hyrule War Table

Battle Royale now boots through the engine file-selection state into a native Fast3D front end. Start, Escape and F1 open the matching match menu. The Shipwright menu is unregistered and the original pause/inventory update and drawing paths are disabled in this edition.

## Player flow

- Launch: create or load `Save/battle-royale.sav`, then show Play in the Temple waiting room. Adventure slot files retain their own paths. Unsupported or unreadable BR saves show an error and remain on disk.
- Play: live Link portrait, battlefield preview, Host Game, Join Game and Practice.
- Lobby: host start, guest readiness, map and match rules, paged player roster and host LAN address/port.
- Practice: all twelve existing Sandbox courses, equipment/bots/bosses/world tools, and solo exploration of playable battlefields.
- Results retain the integrated gameplay recap (standings, score breakdown, replay and spectating); Start opens matching native result actions.
- Start menu: resume, practice tools where applicable, settings, guide and confirmed Quit to Main Menu. Multiplayer simulation continues while menus are open.
- Quit: leave networking, clear pending starts and practice commands, restore overridden state, travel back to the waiting room and return to Play with menu music.
- Settings: audio, graphics, comfort, shadows and lights, host match rules and existing Android updater integration.

Controller uses stick/D-pad to navigate, A to select, B to go back, L/R to change page. Adjustable rows use left/right; mouse/touch uses the left half to decrease and right half to increase. Name/address/port entry includes a controller keyboard. Menu inputs are consumed on opening and closing frames.

## Native presentation

The UI emits textured geometry into the engine display list; it contains no ImGui widgets. A 640 x 360 canvas fits the viewport with letterboxing. The palette is midnight/royal blue, ice-blue selection, ivory text and restrained gold. Bitmap fonts are Bitstream Vera (license beside the font files). Background artwork was generated for this project. Real Link geometry is rendered to the existing pause portrait framebuffer. Custom battlefield previews use terrain colours; original maps use minimaps from the player's game resources.

Selection corners ease to each target, pages slide, and map changes reveal over 180ms. Reduced Motion removes movement and pulses. Music uses the original file-select sequence from installed game resources; selection/confirm/back/error cues reuse original engine sounds at the original pitch with adjustable volume. No ROM music or model data is distributed here.

## Building and maintenance

Apply patches in order and run `scripts/link_mod.sh` as normal. Patch 0025 integrates boot, dedicated save routing, native HUD coverage, transparent portrait framebuffer, and legacy menu removal. `RoyaleWarTable.h` is included once by `RoyaleMod.cpp` after its gameplay services.

To regenerate artwork textures, run `python tools/build_war_table_assets.py` with Pillow. The committed generated header keeps release builds independent of Python/font tooling. RGBA16 background/map tiles are 32 x 32 (2 KiB); IA8 glyphs are 24 x 32. Source art and font license are under `assets/menu/war_table`.

`royale_war_table_tests` covers map cycling, start permissions, input repeat, spatial navigation, animation timing, address/port validation and aspect-fit pointer coordinates. The complete local server/session suite passed after integration (12 tests). `tools/check_war_table_windows.py` supports a supplemental syntax check against the pinned engine and an existing Windows dependency build selected by `ROYALE_WINDOWS_DEPS`; CI remains the full Android compile/link/signing check.

Runtime acceptance still requires a device with extracted game resources: launch/save creation, portrait/render alignment at multiple aspect ratios, controller/touch interaction, audible transitions, two-client host/join/start, practice entry, disconnect and quit/rejoin. Compilation and unit tests do not establish these visual/device results.
