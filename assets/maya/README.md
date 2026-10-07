# Maya companion

Maya is the third selectable cosmetic follower, alongside Lilo and Avriella. Enable the follower under **Your pet**, select **Maya**, then stand still near her and press A to talk. Available in the lobby and on the field; she disappears during the drop, spectating, death, switching pets and leaving the scene.

Original N64-style model based on the supplied family photos: natural child proportions, articulated elbows and knees, rounded polygonal face, brown ponytail and side strands, white printed shirt, blue layered skirt and blue shoes. Family photographs are not embedded or committed. Assets use 32x32 and 64x32 pixel textures, RGBA5551 game exports and batches of at most 32 vertices.

## Animations and props

The rig includes separate thigh/calf and upper arm/forearm joints. Ten clips: idle, walk, run, wave, tablet, draw, pizza, scooter, learn and cheer. Rigged tablet with a block-building screen, sketchbook/pencil, pizza slice, book, and electric scooter with animated wheels. Maya walks and runs after Link, uses the scooter at larger distances, and cycles hobbies when Link stops. Talking plays a wave; 14 rotating lines mention Roblox, Minecraft, drawing, pizza, learning and playful Mom fart jokes. Inactive props are excluded from game drawing. Everything is cosmetic and local to the player.

## Rebuild

Run Blender 5.1 in background mode with `--python tools/maya/build_maya.py`, then `--python tools/maya/export_maya.py`. Both scripts also work from the repository root. Source `maya.blend`, interchange `maya.glb`, texture PNGs and generated `shared/maya_model.h` are included. The `.blend` retains the armature and all ten named animation actions. Previews are rendered on rebuild and ignored by Git.

`server/tests/maya_tests.cpp` validates triangle batches, bone assignments, quaternion normalization, all clips, prop visibility and animation transitions, including Release builds. Full game compilation and handheld playtesting are separate from model validation.
