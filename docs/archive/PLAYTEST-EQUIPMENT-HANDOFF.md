# Playtest equipment and combat fixes — 2026-10-08

Status: source changes complete on `codex/playtest-menu-gameplay-fixes` in `mod-feature-split-worktree`. No APK built, published, or installed.

- Players.inc / SceneHooks.inc / Terrain.inc / Vehicles.inc: keep the shield and worn equipment in the local player draw, including while riding; Goron tunic uses OoT red, Zora tunic uses blue, and the selected skin color remains on Kokiri. Enable and restore adult/persistent mask and cross-age equipment enhancements during the match. Deku Nuts now use their proper hand model/action.
- war_table/Draw.inc: portrait rendering uses the full texture dimensions, fixing the Play/Character menu preview.
- Projectiles.inc / Terrain.inc / server/match.h: shockwave grenades show the thrown model and purple impact, launch nearby players away without damage, and announce the blast even when thrown without a locked target. Launch strength scales with rarity.
- match.h: the first supply drop guarantees a Legendary Gilded Sword.
- EndMatch.inc: keep the death camera on the ragdoll for four seconds before death-card controls or the full results panel take over.
- Players.inc: all songs have a visible effect; Fairy Ocarina and Ocarina of Time now use a generic OoT swirl and particles. Remote spell particles also appear if the caster model has not loaded.
- Projectiles.inc / EndMatch.inc: pickup banners show item descriptions for four seconds.
- Vehicles.inc: local riders use the dressed-player draw path while seated and restore it after dismounting, so equipment and character appearance persist on the cart. Atmosphere.inc: make horizon hills smaller, evenly spaced, and brighter to avoid isolated dark peaks that read as floating formations.
- scripts/test_playtest_equipment.py and server/tests/tests.cpp: cover equipment sync, projectile spawn, knockback-only grenade behavior, and untargeted shockwave throws.

Validation: mod layout passed; extracted equipment/projectile regression passed; server test executable passed, including cart rider simulations; menu matrix regression previously passed with 100 canvas/aspect cases; `git diff --check` passed; the MSVC `/Zs` engine integration compile passed. The attempted CMake configure could not create its generated scratch directory; direct MSVC server tests passed instead. Device visuals and gameplay have not been verified.

Next: finish the `/Zs` check, then build/install the Android APK and verify equipment, menu portraits, ragdoll timing, and shockwave launch on Odin.
