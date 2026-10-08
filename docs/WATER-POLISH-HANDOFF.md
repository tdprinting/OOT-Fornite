# Water polish - 2026-10-08

Reviewed IMG_0027.mov. The clip shows overly bright ripple patterns and blocky shading transitions. Code inspection found coarse/fine shading and height mismatches, hard texture-distance cutoffs, and asymmetric UV headroom.

Changed `mod/Royale/features/Water.inc` to match fine patch boundary positions, colours and UVs to the quantized coarse vertices, fade glint/foam detail by distance, centre texture origins on the nearest tile, explicitly enable bilinear filtering, and apply shallow-water wave damping once. Changed `shared/water_look.h` to soften and smoothly bound glint intensity. Added highlight headroom, fade and disabled-setting checks in `server/tests/water_tests.cpp`.

Validation: MSVC water tests passed; actual water draw-loop allocation harness passed; mod layout and git diff whitespace checks passed. Full RoyaleMod.cpp MSVC syntax check against local engine headers passed. Existing CMake test build configuration has mixed CMake versions; targeted water checks were compiled directly with MSVC.

Next: verify on Odin while swimming, turning the camera, crossing shallow shores, and changing water detail. Device visuals/performance and an Android build are not verified by the local checks.
