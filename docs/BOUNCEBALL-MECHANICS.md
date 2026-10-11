# Bounceball (Shockwave Grenade): mechanics from Tee's reference video

Source: `bounceball.mp4` (Tee's upload, 25.45 s, 1280x720, 60 fps, about 1527 frames). Frame numbers are source frames (frame = seconds x 60).
Frames were pulled with `ffmpeg -vf select=...` and looked at one by one. "Bounceball" in Tee's ask is read as the game's **Shockwave Grenade**:
the video's own title card says "NEW ITEM / SHOCKWAVE GRENADE" (frames 30-120) and the repo has no item called bounceball.

Anything marked **(unverified)** was not seen clearly in a frame: a guess, a number read off a blurry frame, or something the video does not show.

## What the video shows, scene by scene

| Frames | Scene | What is seen |
|---|---|---|
| 0-25 | black | - |
| 30-140 | reveal card | "NEW ITEM!" and "SHOCKWAVE GRENADE" over a purple card. The ball turns slowly. |
| 150-205 | gunfight | No grenade. |
| 210-316 | rooftop, top-down then rising | Throw at own feet, blast, launch straight up past a taller building. |
| 322-406 | sky | A launched player arcs across the sky with a purple streak and fires a rocket down mid-air. |
| 440-545 | field | A sprint with dust at the heels. No grenade. |
| 548-648 | hillside, from behind | Overhand throw while running, the ball bounces ahead, blast at the feet, launch up and over a cliff. |
| 660-726 | sky, far | A tiny figure crosses the sky with white speed lines and a purple dot (unverified that it is the same player). |
| 732-783 | landing | Falls feet first from high up and lands standing on a ledge. |
| 786-880 | town street | The thrower runs holding the ball in the right hand. |
| 884-1000 | wall, side view | The ball rolls along the ground at an enemy, blast, the enemy is thrown far off the cliff. |
| 1010-1160 | dance | No grenade. |
| 1170-1330 | end card | The ball again, "FORTNITE", "PLAY FREE NOW". |
| 1340-1527 | logo | - |

## 1. The ball (model)

- Frames 30-140 and 1230-1290 (close-ups): a **round metal ball** with a **big faceted purple crystal** set in a raised round socket ring on the front, a seam
  running around it, and about **six short round studs** around the body, each with a **glowing purple centre**. The metal looks silver or pale grey, but
  the purple card light tints it, so the true colour is (unverified).
- Frames 786-880: held in one hand, about the size of the fist. It glows purple-blue.
- What the old model has instead: a gunmetal ball with three thin purple rings and a dark cap. There is no crystal and there are no studs.

## 2. Throw and bounce

- **Overhand throw.** Frames 583-592: the arm goes back and up with the ball (586-589), and it leaves the hand at about 592. Frame 210: the ball is
  held up overhead before the throw on the roof.
- **Thrown short, at the thrower's own feet, to launch yourself.** Frames 596-617: the ball is in the air just ahead and to the right of the runner (596),
  lands about one body length ahead (599) and **bounces along the ground with small white dust puffs at each touch** (599-617). The runner keeps
  running and reaches it. On the roof (216-243) the ball sits near the feet with a purple glint.
- **Rolls along the ground at an enemy, leaving a purple streak.** Frames 884-936: the ball skims and rolls low along the floor from the left toward the
  enemy, with a **purple light streak on the ground behind it**. It hops up a little near the end (about 932-936). Who threw it is off screen.
- Spin of the ball in flight: (unverified). It is too small and blurred to see.
- Number of bounces: at least 2-3 dust puffs in 599-617. The exact count is (unverified).

## 3. Fuse and blast

- **The fuse runs on time, not on touch.** The ball bounces and rolls first and blows up later. From release to blast: 592 -> 626 = **34 frames
  (0.57 s)** on the hillside, and about 213 -> 246 = **33 frames (0.55 s)** on the roof (release frame read from a blurry frame, so about +-3 frames).
  The wall scene shows no release.
- **The blast look:**
  - Frame 246 (roof): a big **translucent purple dome** (a half sphere) over the spot. It is wider than the player is tall, about **2-3 player heights
    across** (read from a top-down frame, so (unverified) as a world size).
  - Frame 626 (hill): **white lightning bolts** shooting out and up, and a **bright white-purple ring** flat on the ground under the feet.
  - Frame 940 (wall): the purple dome with white lightning forks inside it. The caught player turns **pink-white**.
  - Frame 944: a **white flash** fills the whole screen for a moment.
  - The dome is fading by frame 249 (roof). It lasts **about 3-6 frames (0.05-0.1 s)** at full size.
- Blast sound: the video has sound, but nothing was checked by ear (unverified).

## 4. Launch height and arc

- **Straight up, and very high.** Roof, frames 250-316: the player rises from a low roof past the top of a taller building (several storeys) and is still
  going up when the shot cuts. The peak is not shown, so the exact height is (unverified). It is many times the player's height.
- **Big sideways carry.** Frames 326-406: a launched player crosses most of the screen sideways in a long, flat arc high in the sky.
- **Tinted while launched.** The player glows **purple-white for about 0.1-0.2 s** after the blast (frames 247-259 and 629-646), then a **purple streak
  trails under the feet the whole way up** (frames 250-316) and through the sky (326-406), with white speed lines (660-726).
- **Can act in the air.** Frames 262-316: a weapon is out and aimed. Frames 366-398: a rocket is fired down mid-air.
- **Steering on the way down:** the drift seen in 660-726 suggests air control. Stick input is not visible, so (unverified).

## 5. Landing

- Frames 732-771: the player falls **feet first, knees tucked, arms out**, with no glider, and lands **standing** at about 765-771. There is **no stagger,
  roll or hard-landing pose** in 771-783, and the next grenade is already in hand (768).
- No damage on landing: there is no HUD in the shot, so (unverified). Fortnite's rule is no fall damage after a shockwave, and the frames fit that.

## 6. Effect on other players

- **Enemies are launched too.** Frames 940-996: the enemy caught in the blast at the wall is thrown **up and far away**, out over the cliff and the water
  (948-996). Two small figures fly off (952-972): two players caught, or the same one seen twice (unverified).
- **Away from the centre:** the enemy flies away from where the ball went off. Exact direction: (unverified).
- Damage to the caught players: no health bar is shown, so (unverified). Fortnite's own rule is no damage.
- The wooden wall near the wall-scene blast breaks into pieces (964-996). Whether the blast broke it, or gunfire did, is (unverified).

## 7. What was wrong in the game's current Shockwave Grenade

1. **You could not launch yourself.** With no target in reach, both the ball you see and the server's blast landed **520 units ahead** (about 10 Links)
   with a **180 radius**, so the blast never reached you on flat ground. Using it to get around, the main thing it is for (frames 210-316, 583-648), did not work.
2. **No bounce or roll.** The ball blew up the instant it touched the floor (`Projectiles.inc`). In the video it bounces with dust puffs, rolls with a purple
   streak and goes off on a timed fuse (0.55-0.57 s).
3. **Two blasts in two places.** The ball you see set off its own blast and its own launch where it landed. The server sent another one, 0.8 s later, at
   the target or 520 ahead. They did not match.
4. **Bots were never thrown.** Only a human's own game launches its player. The server moved nobody, so a grenade thrown at a bot did nothing
   (against frames 940-996).
5. **Fuse too long:** 0.8 s on the server. The video shows about 0.57 s.
6. **Weak blast look.** A bomb-style white ring and sparks. The video has a purple dome, white lightning, a white flash and a purple-white tint on the
   caught player.
7. **Wrong model.** Rings and a dark cap instead of the crystal-and-studs ball (frames 30-140).
8. **The docs disagreed.** `docs/PLAY.md` said "moves nobody else", `docs/ITEMS.md` said "hurts and stuns everyone around the target" and
   `shared/items.h` said "launches nearby players away". The code does none of the "hurts and stuns". The video shows the enemy is launched.
9. **Dead bot code.** `server/bot.h` still treats it as an ability (`p.ability.item == ShockwaveGrenade`), but it is a hotbar weapon now, so that branch
   never runs. Left as is: bots throw it at foes as a weapon.

## 8. How the game does it now (OoT style)

- B with no target in reach: the ball is thrown **110 units ahead** (about two Links), bounces with dust and goes off **0.57 s** after the throw, so it
  launches you. You can run onto it, as in frames 596-626.
- B with a target in reach (a player, a boss or a cart): thrown at them. It rolls the last stretch with a purple trail and goes off at their feet after the same fuse.
- The blast is a server Strike at one spot and time. It does no damage. Every human in **180** is launched up and away by their own game. Every bot in
  180 is thrown up to **260 x rarity** units away from the centre by the server. The client shows one blast: a purple dome ring, white bolts, a flash
  and a light.
- The look follows OoT: the game's own blast rings, kira-kira sparkles and dust, and a flat vertex-coloured ball with a big purple crystal and six
  glowing studs. No Fortnite textures.
- Launch height, fall protection and air steering stay as before (`LaunchSelf` in `Terrain.inc`). The video only shows "much higher than the player",
  so the height is not re-tuned.

## 9. Not checked yet

- No in-game playtest. The mod has not been built here, because the Shipwright fork is not checked out in this container. The bounce, the roll angle,
  the dust, the Strike look and the launch timing are only checked by reading the code.
- The Blender authoring copy (`tools/shockwave_grenade/build_shockwave_grenade.py`) has not been run: there is no Blender in this container. The game
  uses the same shape from `shared/meshes.h`, so it does not wait on it.
- If the server turns a throw down (still on cooldown), your own ball still flies and then fizzles, with no blast.
- The bounces have no sound of their own, because no bounce sound effect is used anywhere in the mod yet.
