# Jones — Astra character design

Design owner: Astra. Implementation owner: Sol 6.1 Medium. Reference: the user's Jones image and Ocarina of Time NPC size chart. The supplied pictures are visual references; their lettering is not part of the asset. User refinement: expressive facial animation, Fortnite-like sound cues, and an attractive low-poly model that is not excessively blocky. The refined geometry and face requirements below supersede the initial conservative triangle budget.

## Visual lock

Jones should read immediately through five shapes: blond angular hair, brown-black sunglasses, red scarf, open parchment coat, and heavy boots. Use the adult OoT townspeople's enlarged head, broad hands, visible joints, and simple shaded surfaces. Keep the reference's square-jawed human face and rounded ears. Do not turn him into Link or add a green tunic/elf ears.

- Height: 1.80 Blender meters, ground at Z=0; body faces -Y, Z up. Game conversion follows the existing exporter (100 units/metre, Y up, forward +Z).
- Proportions: approximately 6.5 heads tall, head 0.28 m high; shoulder width 0.53 m, waist 0.35 m. Hips at 0.90 m, shoulders at 1.43 m. Feet roughly 0.20 x 0.34 m, deliberately broad.
- Hair: a low angular cap and three to five swept wedge tufts. Blond ochre, with a darker side/back cap; no hair cards or many tiny spikes.
- Head: shaped brow, cheekbone, jaw and chin contours; modest protruding wedge nose with a sloped bridge; a distinct lower lip and movable jaw. Add intermediate loops at temple, cheekbone and jaw so the face is recognizably human rather than a bevelled cube. Two rounded rectangular lens planes and a bridge must read at gameplay distance. Glasses are opaque dark brown, with at most one muted painted highlight.
- Coat: sand/parchment tan, open in front from collar to hem. Oversized triangular lapels, dark olive shoulder yoke and cuffs. Hem ends just above knees (~0.68 m). Two broad skirt panels with a rear split; front opening exposes trousers. Outer edge flares slightly. One broad pocket panel each side, no micro buckles.
- Scarf: brick red loop under the chin and one long broad end on Jones's left/front chest ending at upper thigh (~0.87 m). A shorter return may end near belt. Use a few thick angular folds and a jagged two-point hem, no thread geometry.
- Underlayers: charcoal shirt; broad dark diagonal strap; simple brown belt with dull brass rectangular buckle. Muted teal trousers, one olive knee guard on Jones's right knee.
- Hands: dark fingerless glove shapes with skin tip faces; shaped palm taper, curved knuckle line, and a distinct thumb. Fingers may remain grouped, but should form a hand silhouette rather than a rectangular block.
- Boots: dark brown bevelled soles, gray-olive cuff/toe guards, tan strap accent. Use a sloped toe, shaped instep, and narrow ankle rather than stacked cubes.

## Palette (sRGB)

| Material | Main | Shadow/accent |
|---|---|---|
| Skin | `#C99A72` | `#9A664D` |
| Blond hair | `#C5A44D` | `#8B7538` |
| Coat | `#BEB38C` | `#918667` |
| Olive yoke/guards | `#535A45` | `#343B30` |
| Scarf | `#A6332E` | `#702522` |
| Shirt/glasses | `#34302B` | `#211F20` |
| Trousers | `#3E6864` | `#2A4B4B` |
| Leather | `#4A3C30` | `#292722` |
| Boot guards | `#777764` | `#565647` |
| Buckle | `#AD9254` | `#78633C` |

## Mesh and material budget

Aim for 1,800–3,000 triangles, with a practical ceiling of 3,500 if the face and silhouette need it; measure the final mesh. Spend approximately 300–500 triangles on the face, ears and hair before glasses. Use shaped 10–12-sided cross sections for torso/head and 8–10-sided limbs, with intermediate profile rings at chest, waist, elbow, forearm, calf and ankle. Curved silhouette and well-designed proportion matter more than displaying every polygon. Avoid rectangular torso boxes, straight pipe limbs, octahedral sphere joints, and random triangular surface color noise. Preserve clean faceting on hair, lapels, nose and knee guard; use coherent adjacent shades on skin/cloth. No subdivision dependency, normal maps, metallic reflections, or realistic cloth simulation. Small painted seams are optional: vertex-color-only export is valid and matches the existing rigid triangle path. If a texture is supported, a 64x64 nearest-sampled opaque color atlas is sufficient; a dedicated small face strip may store expressions. Do not model seams as raised tubes.

Existing project export uses grouped rigid triangles, so construct separately controllable head, upper/lower arms, legs, coat panels, and scarf. Blender armature/actions should be usable for authoring; exported runtime poses must reproduce their motion or use clearly documented equivalent rigid channels. Avoid skin seams by overlapping joint ends inside sleeves/boots. Coat panels and scarf must have thickness or two-sided rendering where exposed.

## Animation direction

Use stepped/simple 20 fps timing with readable arcs. Idle: 2 s, small weight shift, shoulders breathe, scarf settles. Walk: 0.8–1 s alternating steps and opposite arm swing, restrained coat-tail lag. Run: 0.6 s stronger stride, slight forward lean, scarf trails without entering torso. Greet/hired: 1.2 s short confident right-hand salute, then settle. Attack: deliberate right-arm swing or the game's actual equipped-weapon pose, 0.65–0.9 s; hold readable anticipation for 2–3 frames. Hurt: 0.35 s backward shoulder recoil. Keep feet level at stance frames and avoid ankle skating. No modern motion-capture nuance or continuous high-frequency cloth noise.

## Facial acting

Facial animation must appear in Blender previews and runtime, not only in clip names. Minimum useful channels: `jaw_open` (0–0.025 m lower-jaw motion or ~0–12 degrees), `mouth_smile` (mouth corners lift subtly), and `brow_raise` (visible strip above the sunglasses rises ~0–0.008 m). Opaque lenses hide the eyes, so prioritize mouth/jaw and brows over invisible eye blinks. Blinking is optional only if eyes are actually visible. Keep glasses attached to the upper head; the jaw must not drag the glasses/nose. Keep the scarf below the moving chin.

Required expressions: relaxed closed-mouth idle; brief friendly smile/brow lift when greeted or hired; two or three modest jaw syllable openings during a greeting/reaction cue; focused closed mouth during running; brief mouth opening and cheek/jaw recoil on hurt/attack. A small dark mouth interior must be visible during opening, with one simple light tooth band if needed. Do not place a large detached rectangular mouth tile on the face. Shape-key export, baked vertices, or rigid jaw plus switching mouth shapes are acceptable if the runtime supports them. Do not claim lip sync for nonverbal sound cues; call it expression timing.

## Sound direction

Use actual licensed/project-provided Fortnite sounds if already available and suitable; otherwise create original game-like sounds in a similar broad adventurous/action tone. This design does not prescribe copying or imitating a performer's voice. Minimum events: friendly hire/acknowledgement chime; brief alert cue; cloth/leather movement; footstep or boot scuff; attack swish/impact; hurt/recoil cue. Keep them brief, distinct, and volume-balanced against existing gameplay. Sounds should fire once on the corresponding state transition with cooldowns, not every animation frame. Document their origin honestly and give playable sample paths. Synchronize expressive jaw motion to a greeting/reaction event where used.

## Modeling acceptance

Inspect front, rear, side, and three-quarter renders at 256 px character height, plus a face close-up and a short expression strip or animation. Jones must remain recognizable at that scale. Scarf stays visually separate from the tan coat, coat opening reveals two legs, sunglasses don't blend completely into skin, and all animation poses keep scarf/coat out of thighs and neck. Check that parenting retains world transforms, the model's feet lie at ground Z=0, and render bounds include the entire head and soles. Verify measured height from posed world bounds rather than a hard-coded report value. Compare neutral and greeting/hurt face frames and require a visible change. Save editable `.blend`, interchange `.glb`, preview images, generated runtime header, audio assets and their provenance, and a measured export report. Device appearance and sound timing still require an in-game check.

The optional generated turnaround is modeling guidance. This written design is authoritative if generated views disagree on left/right placement or proportions.
