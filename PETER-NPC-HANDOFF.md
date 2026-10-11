# Peter hireable NPC

Status: implemented. Sixth ally (AllyKind::Peter, index 5), requested by Cynthia. Protocol 39: host and clients update together.

Gameplay: "Peter the Quahog Brawler", 85 rupees, 7 hearts, melee 0.9 hearts / 160 units / 1.5 s, 112 units/s. Standard server hire/follow/combat rules; no private gear (weapon byte stays 0).

Presentation: Blender model + rigid rig + 13 baked clips (`tools/peter/build_peter.py`, geometry `design_geometry.py`), runtime `shared/peter_model.h` + `shared/peter_animation.h`, renderer/behaviour `mod/Royale/RoyalePeter.h`. Cycles four cartoon attacks (haymaker, chicken-fight flurry, belly bump, roadhouse kick) each with its own sound, hit FX and quip; cheer on hire, laugh greeting, knee-grab hurt, chicken-flap dance while owner emotes.
Special: idle phone call with Joe ("Hey Peter!"), 26.6 s, subtitled Zelda-style text box, cancelled by moving/attacking/hurt/hire. Timing shared by animation, audio and subtitles in `tools/peter/dialogue.py`. Cynthia is credited in the call and on his first hire quip.

Audio: original formant synthesis + babble (`tools/peter/build_peter_audio.py` -> `shared/peter_sounds.h`, WAVs in `assets/npcs/peter/audio`). No show audio, recordings or voice imitation.

Network: a sixth ally pushed the 32-player snapshot test over 10 KB/s (10380 B/s). Idle free allies are now sent every 4th snapshot (`kIdleAllyEvery`) and clients keep them `kAllyKeepSeconds` (0.6 s), same pattern as parked carts: 9300 B/s.

Validation: `ctest` 22/22 pass (new Peter hire/melee/cooldown and wire tests). `check_mod_layout.py` passes. RoyaleMod.cpp compiles with gcc and clang `-fsyntax-only` against the fork, no undefined anonymous-namespace symbols (note: engine patches 0005/0009/0013/0015/0022/0025 and three libultraship patches did not apply to this checkout; pre-existing, unrelated). Not verified: device playtest (look, audio mix, call trigger feel, frame cost).

Next: Odin playtest; tune `kPeterIdleBeforeCall`, voice volumes, and the formant words by ear.
