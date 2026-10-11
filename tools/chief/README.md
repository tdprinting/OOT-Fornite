# Master Chief ally (tools/chief)

`build_chief.py` turns the source model into `shared/chief_model.h`: ~1,270 flat vertex-coloured triangles (N64/GameCube budget), an 11-part rigid rig and nine original clips (idle, invite, walk, run, shoot, bash, greet, hurt, death) in a Halo: Combat Evolved-flavoured style. Clips are authored by hand in the script; nothing is taken from Halo files. Guns are Jones's (`shared/jones_guns.h`), mounted in his right hand (`kGunOffset`).

Run (needs `pip install bpy`, Python 3.13):

    python3 -I tools/chief/build_chief.py /path/to/scene.gltf [preview_dir]

The source model and its textures are not in git (24 MB). Download them from the link below.

## Credit (CC-BY-4.0)

Body geometry and colours come from "Master Cheif (Un-Fortnited)" by **bizarrefog1**, licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/):
https://sketchfab.com/3d-models/master-cheif-un-fortnited-e0357f6d6b0d43528cb30e21112a64d7

Changes: decimated from 29,378 to ~1,270 triangles, A-pose arms baked down, re-rigged into rigid parts, colours baked from a 64x64 downsample of the base-colour texture into 5-bit vertex colours, gold visor, new animations.
