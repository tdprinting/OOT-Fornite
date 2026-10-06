# Custom models

Put `dragon.obj` (and `dragon.mtl` if it has colours) in the **models** folder inside the game's data folder (it is made the first time you open the
Royale menu; the menu shows the exact path and whether the model loaded, with a Reload button). It replaces Volvagia, Death Mountain Crater's major boss (the other major bosses always use the game's own models).

- **Format:** Wavefront OBJ, up to 40,000 triangles. Colours come from the `.mtl` (`Kd`) or per-vertex colours (`v x y z r g b`). Textures are not used:
  the game draws these with vertex colours and its own lighting.
- **Animation by part name** (case does not matter): an object or group named with `wing` flaps (left or right is worked out from which side of the
  body it is on), `jaw`/`mouth` opens when the dragon breathes fire, `tail` sways, `head` nods; everything else is the body. A one-piece model still
  bobs, banks and tilts, so it works without any naming.
- **Fitting:** the model is centred, stood on the ground and scaled to a standard wingspan. Add a `dragon.cfg` to change it:
  `scale=1.0` (size multiplier), `yaw=0` (degrees to turn it; try 180 or 90 if it flies backwards or sideways), `lift=0`, `flap=35` (wing beat, degrees).

## Lilo's model (built in, made in Blender)

Lilo the cat is a real low poly model, not code-built shapes: `assets/lilo/lilo.blend` (also `lilo.glb`) has the mesh (about 650 triangles), two small
textures in the N64's sizes (a 64x32 fur atlas and a 32x32 face with the eyes open, half shut and shut), a 25 bone skeleton and eleven actions:
idle, walk, run, jump, sit, talk, groom, sleep, stretch, pounce, happy. The game reads it from `shared/lilo_model.h`, which is generated.

- **Rebuild everything from code:** `python3 tools/lilo/build_lilo.py` (needs Blender, or `pip install bpy==4.2.0`; Python 3.11). It writes the .blend,
  .glb, .fbx and the texture PNGs into `assets/lilo/`.
- **After changing the .blend by hand** (or the build script): `python3 tools/lilo/export_lilo.py` regenerates `shared/lilo_model.h`. Keep the bone
  names, the two materials (`LiloFur`, `LiloFace`), the image names and the action names; actions are keyed at 20 frames a second.
- **Previews:** `python3 tools/lilo/render_previews.py [folder]` renders the poses and a contact sheet.
- In the game she is skinned on the CPU every frame and drawn with her own textures (`DrawLiloModel` in `RoyaleMod.cpp`). Her size is
  `kLiloPetScale` (the pet) and `kLiloMapScale` (Lilo sitting on the map).

## The Lon Lon Buggy (built in, made in Blender)

The cart is a low poly model in the same style: `assets/cart/cart.blend` (also `cart.glb`) has about 1,300 triangles in six rigid parts (the body, the
four wheels and the handlebar, each with its pivot) and one 64x32 texture atlas (`cart_atlas.png`: planks, beams, saddle blanket, iron, cloth, gold,
Goron brick and the glowing firebox). Empties mark the two seats, where riders step off, the chimney top and the flag. The game reads it from
`shared/cart_model.h` and `shared/cart_geometry.h`, which are generated; the physics, the server and the bots use the same measures.

- **Rebuild everything from code:** `python3.11 tools/cart/build_cart.py` (needs `pip install bpy==4.2.0`). It writes the .blend, .glb and the atlas.
- **After changing the .blend by hand** (or the build script): `python3.11 tools/cart/export_cart.py` regenerates both headers. Keep the object names
  (`CartBody`, `WheelFL`, `WheelFR`, `WheelBL`, `WheelBR`, `Handlebar` and the `Seat*`, `Exit*`, `Exhaust`, `FlagTop` empties) and the image name
  `cart_atlas`. Blender's -Y is the cart's front.
- **Previews:** `python3.11 tools/cart/render_previews.py [folder]` renders eight views and a contact sheet.
- In the game the vertices are built once with a fixed light baked in and drawn with one display list per part (`DrawCart` in `RoyaleMod.cpp`);
  the wheels roll and the front pair and the handlebar steer. Riders sit `kSaddleDrop` under the top of the saddle, in Link's own horse-riding poses.

