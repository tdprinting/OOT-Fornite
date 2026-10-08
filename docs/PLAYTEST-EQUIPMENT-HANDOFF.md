# Playtest equipment and menu fixes — 2026-10-08

Status: source changes complete on codex/playtest-menu-gameplay-fixes in mod-feature-split-worktree. No APK built, published, or installed.

- Players.inc / SceneHooks.inc: equipped Goron uses original OoT red (100,20,0), Zora uses blue (0,60,100); Kokiri/no gear retains selected skin. Remote player draws restore the effective local gear color.
- Terrain.inc: synchronize worn tunic and save shield even if actor shield ID is already correct; refresh final hand/sheath model lists before local draw.
- war_table/Draw.inc: load full portrait texture dimensions before binding its framebuffer so UV normalization does not inherit the preceding 32x32 menu tile.
- Projectiles.inc / Terrain.inc: B spawns shockwave grenade variant 10, draws the existing grenade mesh, and emits purple shockwave/light and explosion audio at landing or fuse expiry. Throws also show without a target. Server damage/stun remains on the existing attack-report path; this change does not add server projectile simulation.
- scripts/test_playtest_equipment.py: compiles extracted production gear-color, equipment-sync, and projectile-spawn functions against deterministic stubs.

Validation: mod layout passed; MSVC /Zs engine integration check passed using war-table-compile/gameplay-check.cmd; equipment/grenade production regression passed; menu matrix regression passed with 100 canvas/aspect cases; git diff --check passed. Existing compiler warnings remain. The local engine compile setup and build artifacts are ignored.

Next: build/install Android APK and verify Hylian shield with Basic Sword, shield raising and weapon swaps, Goron/Zora/Kokiri changes with bots visible, Play/Character portrait, and targeted/empty-ground shockwave throws on Odin. Device visuals and gameplay have not been verified.
