# Peter — the Quahog Brawler

Sixth hireable ally (AllyKind::Peter, index 5). Requested by Cynthia as a joke; built as a real game asset. Dedicated to her: she is named in his phone call ("Cynthia, Joe. She said 'put Peter in the game' as a joke. Now I'm a real game asset!"), Joe says hi to her, and the first time he's hired in a match his quip bubble reads "Cynthia put me in this game!".

## Look (OoT x Majora's Mask x Fortnite)

Adult OoT townsperson proportions (oversized head x1.22, broad hands, chunky shoes), flat-shaded low-poly facets with baked key light and height falloff, Fortnite-clean colour blocks. No textures needed: colour is painted per face (vertex colour), the same pipeline as Jones.

- Silhouette: tall egg head, round black-rimmed glasses over big white eyes, bulb nose, double-lobed chin on a hinged jaw, short side-swept brown hair, pear belly in a white collared long-sleeve shirt (placket, buttons, breast pocket), black belt with silver buckle, green trousers, big black shoes.
- 4198 triangles, 12 rigid parts: torso, legs, upper arms, forearms, head, jaw, brows, phone (shown only during the call). ~1.98 m in Blender, drawn at the shared 0.75 ally scale.
- Palette (sRGB): skin `#F3C9A4`, shirt `#F1EFE7`, trousers `#4F7B37`, belt `#26211F`, hair `#4A3322`, glasses `#171717`, phone `#25272C` / screen `#4F8DD0`.

## Animation (baked in Blender, sampled at runtime)

idle, invite (big wave), walk/run (waddle), hurt (grabs his knee: "Ssss... ahh!"), greet ("heh heh heh" belly laugh), cheer ("Freakin' sweet!" double fist pump on hire), dance (chicken-flap dance while his owner emotes), and four attacks he cycles through: haymaker, chicken-fight flurry, belly bump, roadhouse kick. Each attack has its own sound, hit effect (thump / feathers / dust cloud) and an occasional quip bubble. Damage is the server's melee rule for every move (0.9 hearts, 160 units, 1.5 s).

## The phone call (special)

After ~14 s standing idle with a player within 1400 units: the phone rings, he digs it out and holds it to his ear, and Joe: "Hey Peter!". A 26.6 s conversation follows in a Zelda-style text box over his head (speaker name + line), with jaw flaps, nods, a gesturing free hand and a laugh before "Bye, Joe". Moving, attacking or getting hurt hangs up (retry 25 s later); a finished call waits 60 s. Client-local and cosmetic.

## Audio

Original and synthesised (tools/peter/build_peter_audio.py): a formant synthesiser speaks the short words ("Hey Peter!", "Hey Joe!", "Cynthia", "Bye, Joe", "Alright!", "Freakin' sweet!") and Zelda text-box babble carries the rest under subtitles; Joe is squeezed into a phone band. No show audio, recordings, voice cloning or imitation of a real actor's voice is used or committed. See audio/PROVENANCE.md.

## Rebuild

    EGL_PLATFORM=surfaceless python tools/peter/build_peter.py   # needs Blender 5.x (`pip install bpy`) or: blender -b --python tools/peter/build_peter.py
    python tools/peter/build_peter_audio.py                        # numpy

Timing for the call (animation, audio and subtitles) lives in one place: tools/peter/dialogue.py.
