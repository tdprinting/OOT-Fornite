# Player skins: new bodies for Link

A skin is a whole new model for the player that still plays every one of the game's Link animations, because it is bound to the game's
own 21-limb Link skeleton at run time. The original Link stays the default; the choice is on the Character page (**Model**), is saved with
the other menu settings, and travels with your tunic colour so everyone else in the match sees the body you chose. Bots use the game's Link.

Skins live in three places:

| What | Where |
|---|---|
| Source model and textures | `assets/skins/<id>/` (the artist's file plus its textures) |
| Manifest (how the model maps onto Link) | `tools/skins/<id>.json` |
| Generated model | `shared/skin_<id>_model.h` (never edited by hand; hidden from searches by `.rgignore`) |
| Registry | `shared/skins.h` `kSkinModelNames` (the menu name) and `mod/Royale/features/Skins.inc` `kSkinModelOf` (the model behind it), same order |
| Drawing | `mod/Royale/features/Skins.inc` (patch 0031 is the engine hook) |

Shipped: `link_hd` ("Link HD"), the HD Ocarina of Time Link Tee supplied as an FBX.

## Adding a skin

1. **Drop the model in.** Put the rigged model (FBX, glTF/GLB or .blend; it must have an armature with named bones, a T or A pose, Z up
   and facing -Y like a Blender export, or set `up`/`forward` in the manifest) and its textures under `assets/skins/<id>/`.
   The game's texture limit is 256 x 256: every texture is resized to that, so paint for it. Toon outline shells (inverted hulls) should be
   their own materials so they can be dropped.
2. **Write the manifest** `tools/skins/<id>.json` (copy `link_hd.json`):
   - `id`, `name` (what the menu shows), `source`, `textures_dir`.
   - `drop_materials`: materials to delete (outlines, helper geometry).
   - `decimate`: per-object ratio for Blender's collapse decimation. Aim for about 9,000 triangles in all: every player wearing the skin
     is skinned on the CPU every frame, and 32 of them can be in view in the lobby.
   - `textures`: material name -> texture file. Materials sharing a file share one texture slot.
   - `bones`: the rig's bone names -> game limb names. Exact names or `prefix_*` patterns. Game limbs: `root waist lower r_thigh r_shin
     r_foot l_thigh l_shin l_foot upper head hat collar l_shoulder l_forearm l_hand r_shoulder r_forearm r_hand sheath torso`. Fingers go to
     the hand, toes to the foot, spine bones to `waist` (hips and the tunic's skirt, which the game swings as cloth), `lower` (pelvis) and
     `torso` (chest, clavicles). The cap is part of `head`; its tail is carved off by `hat_tail`.
   - `anchors`: for each leg and arm limb and the head, the bone whose head sits at that joint (hip, knee, ankle, shoulder, elbow, wrist,
     neck top). `tips`: for the hands, feet and head, a bone further along (fingertip, toe, top of the head) that gives the limb's direction.
   - `hat_tail`: the hat material and how far behind the head (metres) the tail starts (`start`) and is fully the tail (`full`), and
     `hat_world_dir`: which way the tail should hang when Link stands (Y up, +Z forward; down and back by default).
3. **Build and export** (bpy 4.2 needs Python 3.11: `python3.11 -m venv .venv && .venv/bin/pip install bpy==4.2.0 numpy Pillow`):
   ```sh
   .venv/bin/python -I tools/skins/build_skin.py tools/skins/<id>.json          # -> assets/skins/<id>/<id>_dump.json
   python3 -I tools/skins/export_skin.py tools/skins/<id>.json --dump /tmp/skins   # -> shared/skin_<id>_model.h
   python3 -I tools/skins/preview_skin.py /tmp/skins/<id>.json /tmp/skins/<id>.png  # textured, and coloured by game limb
   ```
   Check the limb colours in the preview: each leg and arm one colour, the tail of the cap its own colour.
4. **Register it.** Add the name to `royale::kSkinModelNames` in `shared/skins.h`, include the header in `mod/Royale/RoyaleMod.cpp` next to
   `skin_link_hd_model.h`, and add `&sk::<id>::kModel` to `kSkinModelOf` in `mod/Royale/features/Skins.inc`, in the same position. The
   static_assert keeps the two lists the same length. Bump `kProtocolVersion` in `shared/protocol.h` (everyone must run the same list).
5. `python scripts/check_mod_layout.py`, then `scripts/check_mod_compile.sh`, then look at it in the game: the sandbox map and the
   Character page picture both draw the skin.

## How the binding works

Nothing from the game's skeleton is stored in the repository. On the first draw of a skin on a skeleton (adult and child Link are
different skeletons; both work), `ComputeSkinBind` poses the game's skeleton in a known standing pose (the last frame of our own landing
clip in `shared/link_anims.h`), reads where its joints are, and matches the model's anchors to them:

- trunk limbs (root, waist, lower, upper, collar, torso, head): one similarity transform, the model's shoulders and hips onto the skeleton's;
- thighs, shins, upper arms, forearms: anchored at their joint, scaled to the game bone's length, turned from the model's T pose to the way
  the game's limb hangs (the smallest rotation between the two bone directions, so knees and elbows keep bending the right way);
- hands follow the forearm's turn and scale; feet the shin's, squashed or stretched so the soles meet the floor;
- the cap's tail is turned about its root from the way the model wears it to `hat_world_dir`.

Each limb's transform is then expressed in that limb's own space, so at draw time a vertex is `limbMatrix * bind * vertex`, blended over its
two limbs, with the scene's sun and ambient light baked into the vertex colour. Patch 0031 calls `OnPlayerSkinLimb` for every limb the game
draws (world, crawling, and the pause and menu pictures); the hook keeps the limb's matrix and blanks the game's display list, then draws
the whole mesh after the last limb. The hands keep the game's own display list while they hold something (the sword or shield is part of
it) and the sheath always does; those batches of the skin are left out. Beyond 1,800 units from the camera the game's Link is drawn instead.
The switch **Skins** under Settings > Advanced turns skin models off for everyone on this device (the game's Link is drawn instead).

## Notes on Link HD

The zip had no readme or licence. The rig's bone names (`ROOTSHJnt`, `l_Arm_ShoulderSHJnt`, ...) are the ones Koei Tecmo and Nintendo use,
so this is almost certainly a rip of an official HD Link model rather than fan art; it is here because Tee supplied it. Its toon outline
shells (`outline_solidify`, `outline_details`) are dropped; its 4096 x 4096 tunic texture is resized to 256 x 256 like the rest.
