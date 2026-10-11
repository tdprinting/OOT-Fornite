# Hireable allies (Kokiri, Zora, Goron, Gerudo)

Original low-poly models in the Ocarina of Time style, one shared 18-bone rig, ten clips each. No ROM or downloaded asset is used.

```sh
pip install bpy==4.2.0 numpy Pillow          # Python 3.11
python tools/allies/build_allies.py           # -> assets/allies/<kind>.blend/.glb, atlas PNGs
python tools/allies/export_allies.py --dump /tmp/allies   # -> shared/ally_<kind>_model.h (+ JSON for the preview)
python tools/allies/preview_allies.py /tmp/allies /tmp/allies/prev   # contact sheets drawn the way the game draws them
```

- Clips (same order for every kind, `royale::allymodel::Clip`): idle, walk, invite (waves you over), attack, attack2 (the Goron's other
  fist), heal (the Zora), hurt, cheer (just hired), rest (sits; the Goron curls into his ball), rise (gets up / unrolls).
- Poses are written with world-axis helpers in `build_allies.py` (`sw` swings a hanging limb forward, `lean` tips the chest or head forward,
  `ab` lifts a limb out to the side, `tw` turns about the vertical). The exporter re-runs the same pose functions and puts the lowest point
  on the ground for planted clips.
- In game: `mod/Royale/features/Companions.inc` (`Ally_Update` picks the clip, `DrawAllyModel` skins and draws). Tests:
  `server/tests/ally_model_tests.cpp`.
