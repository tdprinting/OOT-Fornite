# Sword and shield normal and bump maps

Engine patches `libultraship/0003` and `0024` add scoped surface materials to
the Fast3D interpreter and the OpenGL/GLES shaders used on Android. Equipped
Kokiri, Master and Biggoron swords and Deku, Hylian and Mirror shields use
generated original wood-grain / polished-metal microdetail. Basic Sword uses
the Kokiri model and maps. Both local Link and the standard player puppets
use the same final equipment limb hook. Skin and sheath textures are excluded.

These maps complement the original colour textures. They tile in model space,
including on untextured blades; they do not reproduce the original shield
crest as sculpted relief. Custom Link models that replace native equipment
textures may need their own material assignments.

`scripts/make_item_surface_maps.py` regenerates twelve 64 x 64 RGBA8 PNGs in
`assets/items/surface_maps` and `shared/item_surface_maps.h`. Maps are linear
data with explicit bytes, avoiding the native-word colour bug fixed in PR #79.
Normals use the OpenGL tangent convention; grayscale heights have independent
strength. No ROM or image-generation service is required for regeneration.

The material receives a frame snapshot of the environment's solar light
direction, RGB intensity and ambient RGB. The interpreter transforms that
direction into the limb's model space, and the fragment shader builds the
tangent basis from derivatives, respecting UV handedness. It adjusts existing
shading by the ratio between perturbed and original sunlight, before fog.
Ambient and other existing lights remain in the original shading. This adds
surface relief lighting; it does not add shadow maps or geometry displacement.

Graphics > Sword and shield surface detail controls normal strength (default
100%) and bump strength (default 50%). Set both to zero to disable. Unsupported
render backends, custom shaders, unlit geometry, screen rectangles and combiners
exceeding GLES's attribute budget retain their existing rendering.

Integration with Claude's PR #78: descriptors and four-command material wrappers
use `FrameAlloc` and its double-buffered graphics pool. Materials explicitly
reset before the next limb. They flush renderer batches before changing private
texture bindings (units 6/7); normal colour textures keep their existing cache.
The shader vertex buffer reserves room for the extra attributes. No new effect
layer or translucent pass is needed.

Validation commands:

```text
python scripts/test_item_surface_maps.py
python scripts/check_surface_patch_stack.py
python scripts/test_item_surface_shaders.py  # needs glslangValidator
ctest --test-dir build-item-tests -C Release --output-on-failure
```

The shader test compiles and links the actual GLSL material extension extracted
from the renderer patch for desktop GLSL 130/410 and Android GLES 300, with and
without alpha. Full APK compilation checks engine integration and packaging.
Device checks still required: hold and stow each shield, swing all three swords,
rotate Link at noon/dawn/night, compare both strengths at zero, inspect bots,
raise shields while moving, switch maps and leave a match, and compare frame
rate with a busy lobby. Check that skin and surrounding effects keep their
original appearance and graphics-layer "Left out" counters do not increase.

## The Gilded Sword's own maps

The Gilded Sword (a Blender model drawn by the mod, not one of the game's limb lists: see `docs/CUSTOM_MODELS.md`) has maps of its own that follow its design.
They are computed in `shared/gilded_sword_surface.h` from the same measures as the model (no baked arrays), and each triangle of the model carries a surface
class that picks its map:

| Class | Where | Map |
|---|---|---|
| Blade | the first four diamonds of the blade | 256 x 256, one tile = two diamonds (gold, silver) and the blade's width, so it lands exactly on the model's diamonds: a groove just inside every diamond's edge, concentric engraved diamonds with a raised boss in the gold ones, finer chevrons, an inset border and a pricked centre in the silver ones, brushed lines on the plain metal between, hammered metal under all of it |
| Cord | the red wrapped grip | 64 x 64 diagonal rounded cords with a fine twist |
| Metal | guard, pommel, bands, scabbard trims and the blade's point | 64 x 64 brushed lines and hammered dimples |
| Leather | the scabbard | 64 x 64 pebbled grain |

The sword is drawn lit by the same lights as Link (vertex normals, the material's colour as the primitive colour), with a `gSPSurfaceMap` around each class, so the
sunlight-driven relief and the Graphics > surface detail sliders work on it like on the other swords. `tools/gilded_sword/dump_surface_maps.cpp` dumps the maps for
viewing; PNGs of them are in `assets/gilded_sword/surface_maps`. Device check still needed: swing it at noon, dawn and night and compare the sliders at zero.
