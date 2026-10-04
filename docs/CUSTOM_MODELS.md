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
