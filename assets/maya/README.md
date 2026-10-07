# Maya companion

The third selectable pet, alongside Lilo and Avriella. Maya is a local cosmetic follower. Enable Your pet, select Maya, and stand still nearby to talk with A.

## Art direction

Uses Lilo/Avriella's quantized hand-painted palette, filtered tiny textures and matte N64 materials. Maya has black hair, warm light-brown skin, a white cartoon-print T-shirt and layered blue skirt. Continuous shirt/shoulder/arm topology shares vertices; arm and knee joints blend across two bones. The authored proportions are those of a child, with an in-game scale of 0.41 (about 56 units tall compared to young Link's roughly 60). Talk focus height follows that scale. Family photos are not embedded or committed.

## Motion and expressions

Seventeen clips: idle, walk, run, wave, tablet, draw, pizza, scooter, learn, cheer, talk, giggle, hop, fidget, point, sit and sleep. Walk and run have heel strike, foot roll, knee lift, hip sway and a body bob that comes from planting the lowest foot on the floor (runs add a short flight); the game plays them at `speed / stride` (`kWalkStride`, `kRunStride` in `shared/maya_anim.h`) so her feet do not skate. After about 24 s of standing still she sits, after about 45 s she hugs her knees and dozes. Her skirt is one smooth two-tier mesh whose hem follows the nearer leg, and her ponytail is a two-joint weighted chain with a swept fringe over the forehead. Authored anticipation, settling, relaxed wrists, counter-rotation, head tilts, toe fidgets and trailing ponytail motion replace uniform robot swings. Eight painted expressions include speech, concentration, winks and laughter, plus independent half/closed/half blinks.

Props use baked two-bone arm IK: palms meet tablet/book edges and scooter grips, pizza travels with her hand, and the drawing pencil tip tracks the page. Tablet use includes a downward gaze, two distinct index-finger taps, a drag across controls and a pleased reaction; other fingers curl naturally. The wave uses a bent elbow with the hand beside her face and a wrist flutter; the lowering path stays outside her torso. The screen and drawing surface face Maya and angle upward. Joint links are preserved while interpolating and cross-fading so elbows, wrists and knees remain connected instead of shortening or kinking. Pencil and book have separate joints. The game only samples baked clips; it does not run IK at runtime. Follow speed accelerates gradually, turning is smoothed, and clip changes cross-fade. Fourteen dialogue lines retain her hobbies and playful Mom fart jokes.

## Rebuild and review

From the repository root: `blender -b --python tools/maya/build_maya.py`, then `blender -b --python tools/maya/export_maya.py`. `personality.py` authors motion and painted textures. `preview_motion.py` renders review frames. Source Blender/GLB, texture PNGs and generated game data are included. Generated previews are ignored by Git.

`server/tests/maya_tests.cpp` checks mesh batches, all joint transforms and clips, expression frames and blinks, child scale, blended skinning, prop visibility, cross-fades, hand contact throughout tablet/drawing/scooter clips, and pencil-tip contact with the page. Full game compilation and actual handheld playtesting remain separate checks.

## Real voice

Two user-confirmed Maya-only playful responses from the supplied family recording are included as mono 16 kHz PCM16 WAVs. The opening adult line and surrounding adult speech are excluded. Clips have light background-noise cleanup, fades and matched levels without pitch or speed changes. They play on greeting, conversation and happy reactions through a dedicated mixer voice, with cooldowns, game-volume support and a voice toggle. Switching pets, leaving a scene or disabling voice stops playback. The original MOV and its metadata are not included. Rebuild the game header with `python scripts/make_maya_sounds.py`; `sounds/manifest.json` records the selected source intervals.
