# Jones hireable NPC

Status: source integration implemented; Astra owns final Blender appearance and asset export, Sol owns gameplay/audio/network integration. Jones is appended as ally kind/index 4, so existing four NPC indices stay unchanged. Protocol 37 requires host and clients to update together.

Gameplay: Jones the Rift Scout costs 100 rupees, has 5 hearts and follows at 125 units/sec. Four private rifles select by target distance: compact carbine below 275 units (.30 hearts/.75 sec), tan assault below 550 (.45/1.10), olive burst below 850 (.60/1.50), scoped beyond that (.85/2.10; maximum1250 units). Server rules choose the weapon, check range/LOS/cooldown and credit damage to the owner. These are a separate NPC enum with no player ItemIds, ammo, inventory, pickups or drops. Snapshots and reliable attack events carry the server-selected rifle; each has its own visual mesh/report, with recoil, muzzle flash and a brief tracer.

Existing server checks enforce proximity, affordability, owner state and two-helper cap. Existing spawning supports all maps; maps with only four authored stations place the fifth on valid ground through the normal fallback.

Presentation: `tools/jones/build_jones.py` authors the Blender asset, rigid body/facial controls and baked runtime rotations. `shared/jones_model.h` is generated. `shared/jones_animation.h` samples the clips; `mod/Royale/RoyaleJones.h` renders within a reserved graphics pool budget. Jones uses idle/invite/walk/run/attack/greet/hurt clips, with jaw and brows inheriting the head. Nearby greeting is rate limited to 12 seconds; hiring, attack and health loss trigger reactions. Snapshots edge-detect attacks to avoid repeated sounds. The normal ally label, hire action, minimap and network ownership flow include Jones.

Audio: seven original short synthesized cues (greet, hire, hurt and four distinct rifle reports), authored with `python tools/jones/build_jones_audio.py`; WAVs and provenance are in `assets/npcs/jones/audio`. They follow adventure/shooter direction and are not Fortnite recordings. Embedded PCM uses the existing mixer, separate voice slots and distance attenuation.

Validation before latest-base adaptation: focused server/network suites passed 2/2 with the final 3652-triangle, 14-part body and four private gun exports (304/244/324/420 triangles). Coverage includes hire/refusal/payment/cap/following/owner credit, rifle distance selection/cooldown/range, no rifle drops on release/fall/owner death, protocol validation, body/gun/parent bounds, finite animation samples, jaw/brow motion and audio amplitudes. Layout and diff checks passed on that earlier base.

Latest-base integration: preserved the current four custom NPC models/animations and potion use/drop protocol, adding Jones as a separate renderer/update branch. The user explicitly requested no further checks; this final adaptation was not rebuilt or retested. Do not treat the earlier results as validation of this adaptation.

Engine validation: isolated Jones renderer compiled with MSVC against real local Shipwright engine graphics, actor and ImGui headers, including the full draw-budget guard and facial head hierarchy. Full `RoyaleMod` compilation is unavailable because the local engine lacks the required patched `surface_map` API and this checkout lacks complete Android fork dependencies. This check does not prove a full engine build or device behavior.

Remaining verification: full patched engine/Android build and Odin device playtest are required to verify final appearance, audio mixing, targeting, follow motion and frame cost. Root owns the authorized merge; Sol did not publish or release. Preserved unrelated `assets/bosses/morrow/morrow.blend1` and excluded unrelated Morrow implementation from the Jones merge.
