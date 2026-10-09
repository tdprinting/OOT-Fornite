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

## Startup, pause and elimination isolation

Menus are drawn directly from a dedicated Play_Draw branch, before any world display lists are submitted. The branch clears the main framebuffer and draws only the menu and its optional character portrait. It does not depend on OnPlayDrawEnd or inherit scene depth, alpha, grayscale, material or framebuffer state. The Temple remains loaded to supply game services and the portrait; it is never drawn behind an open menu. Match simulation continues.

World sound requests are rejected while a menu is open; only native menu and recap navigation cues are accepted. Existing sound effects stop at menu entry. Sub music and fanfares are silent, main music plays the menu sequence, and custom PCM voices advance silently so an old voice does not restart on Resume. Gameplay volume returns on close.

The BR pause screen has a large Resume Match action, Settings, How to Play, Character or Practice Tools, and confirmed Leave Match. It shows the battlefield, survivors and elimination count, with a clear Match Continues notice. The native death menu opens after the 1.8 second ragdoll moment and shows placement, eliminator, eliminations, damage and points. A selects; B spectates; Leave uses the same confirmed exit as pause. Start from spectating opens the death actions. The legacy death card no longer draws competing buttons.

Device acceptance: launch from a cold start, use every top tab, enter Practice, open/close pause, die and choose Spectate, reopen with Start, confirm/cancel Leave, and verify no Temple/Link/world audio during any menu. Check 16:9 and 4:3. Windows engine syntax and unit checks cannot confirm the Android device presentation.

The pause status panel displays live health/max health, potion shield, normal attack per hit/projectile, passive normal-hit defense, and magic. It uses the server's weapon, ammo fallback, rarity, gear, equipped shield and Adult Power formulas. Attack excludes jump/spin and target-specific modifiers; defense excludes guard direction, piercing, temporary potion reduction and damage-type-specific modifiers. The detailed match recap also suppresses world rendering and audio.

HUD vitals and item/gear hotbars now share the native palette, square framed panels, gold corner ornaments and ice-blue selection. Rarity remains a separate coloured stripe and icon. Health/shield/magic replace the original adventure HUD during BR sessions; interaction and cooldown logic are unchanged.

## Game options and pet previews

Settings now starts with a visible Pets & Preview category. Pets is the master switch; Lobby Pets controls the trio and Follow Me controls the selected gameplay companion. The master switch preserves both saved choices while disabling all three companion spawns and their pending voices. The Character page links directly here. Preview remains available when pets are off.

Lilo, Avriella and Maya are animated from their existing skinned game meshes in a dedicated 192 x 192 framebuffer. Bounds fit the selected pose, matrices use packed engine format, depth clears every frame, and menu UV normalization uses the full framebuffer dimensions. No preview actor, dialogue, collision or world drawing is created. Reduced Motion freezes preview animation; its separate animation button changes only the preview.

The settings sidebar has two pages. Option pages expose sky/ground weather, water detail and effects, combat particles/glow, music volumes/folder playback, cloth/wind strength, hosted season/weather changes and existing feature switches. Direct settings edits refresh cached runtime graphics values immediately and save the original CVar keys. Match options retain host/lobby permissions.
