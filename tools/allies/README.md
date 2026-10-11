# Hireable allies (Kokiri, Zora, Goron, Gerudo)

Original stylised models, a blend of Ocarina of Time, Majora's Mask and Fortnite, one shared 18-bone rig, eleven clips each. No ROM or
downloaded asset is used. Reference pictures Tee picked are in the project files under `art-style/npc-refs` (the OoT Kokiri artwork, the
remake's Goron and Zora, Gerudo Valley and Nabooru).

```sh
pip install bpy==4.2.0 numpy Pillow          # Python 3.11
python tools/allies/build_allies.py           # -> assets/allies/<kind>.blend/.glb, <kind>_body.png, <kind>_face_<expression>.png
python tools/allies/export_allies.py --dump /tmp/allies   # -> shared/ally_<kind>_model.h (+ JSON for the preview)
python tools/allies/preview_allies.py /tmp/allies /tmp/allies/prev   # contact sheets drawn the way the game draws them
```

- Geometry (`build_allies.py`): bodies and limbs are smooth lofts (`tube`) through the joints with weights blended between the nearest two
  bones, so elbows, knees and waists bend without seams; heads are deformed ellipsoids (`blob`) with a painted face grid on the front; hair
  and fur are curved tufts; fins and cloth flaps are thin slabs.
- Textures (`paint_allies.py`): a 256 x 256 atlas of 32 x 32 material cells painted from code (woven cloth with frayed hems, fur strands,
  rock, iron, gold, Zora scales, wood, leather, a Goron belly with a navel), and a 128 x 64 face in three expressions (normal, blink, fierce).
  Each part maps its own 0..1 UVs into its material's cell.
- Clips (same order for every kind, `royale::allymodel::Clip`): idle, walk, invite (waves you over), attack, attack2 (the Goron's other
  fist), heal (the Zora), hurt, cheer (just hired), rest (sits; the Goron curls into his ball), rise (gets up / unrolls), dance (when the
  owner does an emote or wins).
- Poses are written with world-axis helpers (`sw` swings a hanging limb forward, `lean` tips the chest or head forward, `ab` lifts a limb out
  to the side, `tw` turns about the vertical). The exporter re-runs the same pose functions and puts the lowest point on the ground for
  planted clips.
- In game: `mod/Royale/features/Companions.inc` (`Ally_Update` picks the clip, `DrawAllyModel` skins and draws with the two textures).
  Tests: `server/tests/ally_model_tests.cpp`.
