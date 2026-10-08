# October 8 playtest fixes

Implemented: fixed native startup/pause modelview matrix encoding; preserved NPC
message drawing when hiding the adventure HUD; restored native Hookshot mapping;
removed stale shieldless guard/stance poses; retained Adult Link and timeless-item
support; synchronized glider hanging joints and actual hand spacing; supplied the
reef tank collision camera table; allowed snow alongside scene fairies without
spawning duplicate snowfall actors.

Victory: seven seconds of visible celebration before results capture controls;
winning humans may walk/emote, bot winners perform the Victory emote, eliminated
viewers follow the winner, and a Blender-authored Triforce/spiritual-stone crown
is drawn on the winner's head. Original OoT hearts and magic-meter textures are
used for health, magic and the blue shield meter, with resource-pack fallbacks.

Visual assets: four connected Blender-authored trees replace standard procedural
trees, each within the existing 420-triangle mobile budget. The user's tree photo
was not attached; these replace the standard foliage rather than a photo-matched
landmark. Editable .blend files and previews are under assets/scenery and assets/victory.

Normals: small smoothed maps derive once from the actual currently loaded diffuse
textures, including original UVs and palette formats. Invented procedural stripes
are replaced by neutral one-pixel fallbacks. Runtime asset resources remain the
user's own; no ROM textures are embedded. Unsupported backends/untextured faces
retain original rendering. Cache and input dimensions are bounded.

Shadows: Astra design, Sol 6.1 implementation. Actual collision triangles replace
stitched floor grids, with clipping, texel antialiasing, zero-softness handling,
low-sun padding, fixed budgets, and native fallback. Props/chests default on.
Raised receivers above caster base, vertical walls and exhaustive stacked-floor
coverage remain outside this approximation; see DYNAMIC_SHADOWS.md.

Source efficiency: menu split into five focused include fragments and indexed in
mod/Royale/war_table/INDEX.md. Original vitals/crown are separate feature files.
Feature packaging checks verify the new fragments. The split preserves include
order and exactly reconstructs the original menu source before behavior changes.

Validation: 18/18 CTest tests passed. Production matrix regression reproduces old
zero-w and passes 100 canvas/aspect cases. Water draw buffer bounds, material
batching/texture-unit regressions (including negative controls), generated neutral
maps and strict native patch application/reversal pass. The native patch checker
now includes historical plain unified diffs rather than silently skipping them.
Kingdom climbing is tested against authored ivy routes rather than depending on
random bot goals after a warmup. Full mod/renderer Windows syntax and Android CI
results are recorded in the final chat handoff.

Device acceptance is still necessary: cold startup, Start pause/reopen, page
navigation, NPC talk/close, Adult Link's child-only items and Hookshot shots/pulls,
snow, reef-tank jumping, glider grips, player/bot victories and shadows at steps,
rocks, bridge edges and dusk. Builds do not establish device visual correctness.

User authorized resolving conflicts, merging, and publishing a testable Android
build. Default branch was claude/happy-einstein-1xydck at 623c9727 before publishing.
