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

