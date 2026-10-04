# How to get and install OOT Royale (no building needed)

GitHub builds the game for you automatically. You download the finished files from your repo's **Actions** page.

> This is an early test build. **Nobody has launched it yet**, so expect bugs. Use a throwaway save file.

## You will need
- A GitHub login that can open the `tdprinting/OOT-Fornite` repo (the files are private to you).
- **Your own copy of the Ocarina of Time ROM file.** The game asks for it the first time you start it. It is never included in the download.

## Step 1: download the files

1. Open <https://github.com/tdprinting/OOT-Fornite/actions/workflows/game-build.yml>
2. Click the newest run with a **green tick** .
3. Scroll to the bottom of the run page to **Artifacts** and click to download:
   - **royale-android-apk** for your Odin 2 Portal or any Android phone
4. The download is a `.zip`. Unzip it.

A red cross on a run means that build failed, and there's nothing to download from that platform for that run. Tell me and I'll fix it.

## Android (Odin 2 Portal)

The app installs as its own app, **TDawgs Battle Royale** (package `com.tdawg.battleroyale`), so it sits next to your normal Ship of Harkinian without touching it. It has its own saves, settings, game data and mods in a separate folder on the device called `TDawgsBattleRoyale` (the normal one uses `SOH`). Because it is a different app it asks for its own permissions and its own ROM the first time.

1. Copy the unzipped `.apk` file to the device (USB cable, or a cloud drive).
2. Open it from the Files app and allow **Install unknown apps** when asked.
3. Open **TDawgs Battle Royale** and **allow all file permissions** (this is a separate permission from the other app's).
4. When asked, answer **Yes** to generating the data file, **Yes** to looking for a ROM, and pick your Ocarina of Time ROM file. Wait for the extraction to finish. (To skip picking it again you can copy `OOT.z64` from the `SOH` folder into `TDawgsBattleRoyale`.)
5. Press **Back / Select / -** on the controller to open the menu.

Your existing Ship of Harkinian saves are not shared with this app. If you want one, copy the save file from `SOH/Save` into `TDawgsBattleRoyale/Save`, but a throwaway save is safer while this build is untested.

## Starting from the file select
When you make a new save, the quest picker (the screen where you choose between the normal game, Master Quest, Randomizer and Boss Rush) now has a fifth option, **Battle Royale**. Pick it and finish creating the save as usual. It is a normal save underneath, and the game remembers which saves were made this way. A few seconds after you load one, the game's menu opens on the **Battle Royale** page so you can host a lobby or join one straight away. You can always open that page yourself from the menu (Back / Select / -).

## Playing a match

**Important: the game plays exactly like normal Ocarina of Time until you Host or Join from the menu.** Walking around Hyrule Field
does nothing by itself. Nothing is added to the world until a lobby is open.

1. Start the game and load any save file (use a throwaway one).
2. Open the menu (**Back / Select / -**) and choose **Battle Royale**.
3. **To host:** type a name and press **Host a lobby**. The lobby screen shows your address (for example `192.168.1.23:7777`) with a **Copy** button. Tell your friends the numbers before the colon.
   - Same Wi-Fi: that address works as shown.
   - Over the internet: use a free VPN like Tailscale on every device (easiest), or forward UDP port 7777 on your router.
4. **To join:** type the host's address and press **Join lobby**.
5. You land in the lobby: a player list with the host marked, a **ready** button for everyone but the host, and (if you left the option on) you are taken to the Temple of Time to wait together.
6. In the lobby the host can set the **number of players** with a slider from 2 to 32 (bots fill whatever the people don't; smaller matches also get fewer towns and mini bosses). A timer counts down from 2 minutes and the match **starts by itself** when it reaches zero (the host can turn that off with a checkbox). Everyone sees the countdown.
8. The host picks **Bot difficulty** (Easy, Normal or Hard) and presses **Start match** (or waits for the timer). Empty spots fill with bots up to 32 players. After a 10 second countdown everyone is moved to Hyrule Field automatically.

You can play alone: press **Host a lobby**, then **Start match**, and you will face bots.

### If something goes wrong
- Nothing happens on Join: check both devices are on the same network or VPN, the address is right, and the firewall allows the game.
- The game looks completely normal: you have not hosted or joined yet. Open the **Battle Royale** menu.
- The window says "Host runs a different version": everyone must install the same build.
- Crashes or odd behavior: tell me what you were doing. This build has never been run, so those reports are exactly what I need.

## How a match starts
1. **Splash screen:** each match opens with a title card (Triforce, gold lettering, "Made by Tevin Dahl") for the first 5 seconds of the countdown while everyone is moved to Hyrule Field.
2. **Hanging in the sky:** you spawn high above your spawn point and wait for the countdown to end.
3. **Skydive:** when the drop starts you fall from the sky. The stick steers, hold **Z** to dive faster. You are protected from damage until the drop ends (18 seconds), and anyone still in the air then plummets to the ground. Bots land at the same time you do.

## Controls in a match
- **Treasure chests.** All the loot is in glowing chests, coloured by rarity (grey Common, green Uncommon, blue Rare, purple Epic, gold Legendary). Walk up and press **A** to open one. You see how rare a chest is, not what is inside. Epic and Legendary chests are the big kind with golden light. A beam and glow mark chests on your screen, and the map shows them.
- **Items dropped by players** (when someone is eliminated or swaps a weapon) lie on the ground as coloured rupees with their name. Walking over one picks it up only if it is an upgrade (a better weapon, a spare weapon for an empty slot, a shield or gear that beats yours, a potion if your bag has room, an ability if you have none). For anything else stand next to it and press **A** or **D-pad Right**, which swaps it for what you hold.
- **The item bar** at the bottom works like Fortnite's hotbar: three weapon slots (the one in your hand is highlighted), then shield, potions and ability. **D-pad Left** switches to the next weapon. On a touch screen you can tap a slot. Slots are icons edged in their rarity colour; only the weapon in hand is named. One short line above the bar names each pickup, and the menu keeps a colour-coded list of recent pickups.
- **B** attacks with the weapon in hand (its damage and range come from the item and its rarity, not from your sword). Some weapons add effects: Fire Arrows burn, Ice Arrows freeze, Deku Nuts stun, Light Arrows ignore shields, Bombs hurt everyone near the target.
- **D-pad Down** drinks a potion (the best one for how hurt you are). A Fairy in your bag is never drunk: it revives you once if you would die.
- **D-pad Up** uses your ability, then it recharges. The slot shows the countdown.
- Gear works on its own once you pick it up. Heart Pieces and Heart Containers raise your maximum hearts.
- **Other players** wear a nameplate with their name, hearts left and weapon, colour coded by rarity, so you can size them up. The map (bottom left) shows the field, the safe zone, chests, other players and you.
- **The storm** has no wall to see (a wall drawn over the game showed through hills and buildings). The edge is on the map (purple outside, white circle inside), the timer top left says when it closes, and an arrow points to the safe zone. Outside it the sky goes dark, a violet haze, rain, wind streaks and lightning fill the screen, and it hurts. Each match the circles are different, and small maps have a slightly gentler storm.
- **Towns.** The field has up to twelve named points of interest with silly rhyming names (Deku Dew Zoo, Goron Groove Lagoon, Zora Snore Shore, Navi Gravy Bay and more), laid out like a battle royale map: a big landmark in the middle (Hylian Billion Pavilion), a ring of towns around it and a wider ring near the edge. Each has a walled stone building with a roof and a door, a small cave (a horseshoe of boulders) and some ruins, and most of the chests are inside them. A few are scattered between towns. Town names float over each place (big enough to read while you skydive), show on the map, and a message tells you when you walk in.
- The open ground between towns has rocks, boulders and bushes to hide behind. The same ones appear for everyone, and bots path around the solid ones and through the doors.
- **Mini bosses.** Up to five golems (Stone Moan, Lava Java, Frost Lost) guard the caves, bigger and tougher in that order. They notice you at a distance, chase you slower than you run, smash you for a heart or more and give up if you get far away. Hit them with **B** like any player (you can hit them from a little further off, because they are big). A health bar floats over each, and they show as purple diamonds on the map. When one falls it drops three to five Epic and Legendary chests around it, you score 400 points for the last hit, and bots go after them too if they are strong enough (weak ones keep away).
- **Falling limp.** When a player is eliminated their body is thrown back, bounces, slides and settles in Link's knocked-down pose, then fades after a while. Yours too.
- **Emotes.** The **EMOTE** button (bottom right, tap it) opens a list, and **C-Right** plays them in turn: Wow!, Admire your hands, Look to the sky, Admire your sword, and the **Chicken dance**, a custom routine (beak, wings, tail shake, clap) with its own little polka tune. Everyone near you sees it, and hears the tune when you do the dance, quieter the further away they are. Moving, attacking or using an item stops it. The emotes are performed by a copy of your character while the real one is hidden.
- **Shockwave Grenade.** A new ability item (Uncommon to Legendary): blasts everyone within a few body-lengths straight away from you, no damage, and leaves them dazed for under a second. Use it to escape or to knock someone off a ledge toward the storm.
- **Sprinting.** Click the **left stick** while running to sprint, like Fortnite. Link really runs faster, so his legs pump quicker, his footsteps speed up and dust kicks up from his heels; other players see you sprint the same way. A thin gold stamina bar appears under the magic meter and drains while you sprint (a full bar lasts about six seconds). Letting go of the stick, clicking again or running dry stops it; the bar refills after a second's rest and fades away when full. It turns red while you are too winded to start again. No sprinting while skydiving, swimming, stunned or shielding.
- **Jumping.** **C-Up** jumps. If there is a ledge about knee to chest high right in front of you, the jump pulls you up onto it.
- **Glints.** Chests and dropped items throw off sparkles in their rarity colour, more often the rarer they are.
- **Minimap options.** In the Battle Royale menu, **Minimap options** lets you show or hide other players, bots, mini bosses and chests.
- **Time of day.** The game's own day and night lighting runs through the match: it starts in the morning and the last circles are at dusk and in the dark.
- **Custom models.** The stone posts, boulders, rocks and cottage roofs are drawn from low-poly models made for this mod, in the flat chunky Ocarina of Time style, instead of the game's own rock models. There is a checkbox in the Battle Royale menu ("Custom rocks and buildings"). If the game ever crashes or the scenery looks wrong when a match starts, turn it off and tell me.
- If you are eliminated you become an invisible spectator and can keep walking around while the match finishes.

## Customize character
In the Battle Royale menu press **Customize character**. Pick one of ten preset skins (Hero of Time, Hero of the Winds, Goron Red, Zora Blue, Gerudo Gold, Shadow, Sheikah Crimson, Moblin Magenta, Snowhead Frost, Midnight) or choose **Custom colour** and mix your own. It sets the colour of Link's tunic. Everyone in the match sees you in your colour, and bots wear presets. Your choice is sent when you connect, so change it before hosting or joining. It is saved between sessions.

Skins are colours on the game's own Link model, not new models. If you install a Link model pack in Ship of Harkinian on your own device, that changes how *you* see every player (they are all drawn with the same Link model), but other players don't see your model, because only your colour and name are sent over the network. For everyone to see the same model, everyone installs the same pack. Whether a pack's tunic follows the colour depends on the pack.

## Points and playing again
You score points for damage dealt (100 per heart), kills (500), chests opened (25), how long you last, and 1000 for winning. When the match ends the standings show everyone's kills, damage and points. The host presses **A** (or **Play again** in the menu) to start a new match straight away with everyone who is still connected, with fresh chests, scenery and storm.

## The map viewer
Open `tools/map-viewer.html` in a browser (it is in the repo, no install). It lists every item, the rarity tiers and the scenery, and shows a sample map. To see a real map: in a lobby, open **Battle Royale, Developer tools, Export map data**. That writes `royale-map.json` into the game's data folder; load it in the viewer to see the field, the storm circles, every chest (hover to see what is inside), the scenery and the players.

## What works in this build, and what doesn't yet
- Everything is written and compiled, and the server side is unit-tested (and a bot-only match simulator, `royale_balance`, is used to tune it), but the game side has only been played a little. Expect rough edges.
- The map size is measured automatically when the host presses Start (the host is taken to Hyrule Field first), and loot, spawn points and the storm are kept on ground that exists. Bots walk in straight lines, so they can walk through walls and water.
- Weapons are drawn in other players' hands and arrows, bombs and shots fly; shields and gear are still not drawn on other players. The item list is long (90 items), so expect balance problems.
- Bots now find their way around obstacles in Hyrule Field if the map probe found them, strafe and dodge in fights, flee losing fights, use abilities and hunt in the endgame. They still can't see the difference between a ledge and a cliff, and they see through walls.


## Weapon glow and lobby music

- **Weapon glow:** whatever weapon a player holds gives off glints in its rarity colour (grey, green, blue, purple, gold), more of them the rarer it is. It is on for other players by default; "Glow on your own weapon too" is off by default. Both are checkboxes under "Minimap and game options" in the Royale menu.
- **Lobby music:** put `.wav` files in the `music` folder inside the game's data folder (it is created the first time you reach a lobby). They play shuffled while you wait in the lobby and stop when the countdown starts. Only WAV files work (the game has no MP3/OGG decoder). Turn it off with "Play songs from the music folder in the lobby".
- **OoT instruments:** with "Play the music folder's songs with Ocarina of Time's own instruments" on (it is on by default), the game turns each song in the folder into real Ocarina of Time music and plays it on its own sound engine, the same way it plays its own songs. It finds the song's notes and drums, splits them into melody, harmony, bass and drums, and picks the game's instruments and drum kit that sound most like each part (the same steps as `tools/song-to-oot.html`). Songs are converted one at a time in the background, about ten times faster than they play; until a song is ready its original plays. Each converted song is kept in `music/.oot`, so it is only converted once (delete that folder to convert them again). The Royale menu shows how many are ready.


## Maps
The host picks the place in the lobby: **Hyrule Field** (a huge arena with places of its own: a ruined castle, ranch, great wall, ravine, stone circle, graveyard, windmill hill and a raised causeway), **Lake Hylia**, **Kakariko Village**, **Death Mountain Crater** and **Desert Colossus**. Each has its own themed mini bosses (golems of stone, moss, tide, frost, shade, lava or dune), its own dragon, and 24 silly place names. The sizes come from the game's real collision data (see `docs/MAPS.md`). You cannot walk out of the map: every door, cave mouth and map edge that would load another scene is sealed.

## Everything else that is in
- **Seasons and weather.** The host chooses a season (or random), how strong the weather is and how often it changes. Each map has weather of its own: rain, thunderstorms (lightning strikes at marked circles), fog, snow in winter, ash in the crater, sandstorms in the desert. Fog and sand hide you from bots, rain puts out fire, ash feeds it. Everyone can turn the screen effects down or off ("Weather effects on my screen").
- **Economy.** Rupees and ammo come from bushes and rocks and from eliminated players (about half their items and 60% of their money and ammo). You start with a basic sword (weak but free). Bows, slingshots, bombs, bombchus and nuts need ammo. Supply drops announce themselves and land a crate of Legendary loot where the safe zone will still be.
- **Special weapons.** Triple Slingshot (three shots), Giant's Hammer (a big blow with a wide radius), purple Homing Bombchus that chase people.
- **Hireable allies.** A Kokiri slinger (40 rupees), a Zora tidecaller who heals you (70), a Goron brawler (90) and a Gerudo archer (110) wait around the map. Walk up and press **A** to hire one (two at most). They follow you and fight for you until you are eliminated, then they are free again.
- **Magic.** A green bar under your shield bar. Abilities cost magic (the white tick shows what yours costs), it refills slowly, and Magic Jars top it up.
- **Heart Container chests.** A few hidden and climb chests are pink and hold an extra heart. **Adult Power**, a very rare Legendary find, makes you bigger for a minute: you hit harder, take less and run faster.
- **Climbs.** Stone block staircases (jump and clamber) with the best chest on top; some chests are hidden behind boulders.
- **Cloth and wind.** The glider's canopy is simulated cloth and Link's cap swings in the wind. There is a slider in the lobby for how much.
- **The sign and Maya.** A sign stands in the middle of every map and a little Kokiri called Maya somewhere on it: walk up to the sign to read it, press **A** next to Maya to talk to her.
- **Match replay.** When a match ends, a top-down replay of it plays beside the results.
- **Music.** Lobby music from the music folder, and "Match music": the game's own, random songs from the folder, or none.
- **Controls added:** **C-Left** drinks a shield potion, **C-Up** jumps, **C-Right** emotes, **A** also hires allies and talks.
- **Lilo the cat.** An Easter egg: a grey and white cat called Lilo sits at a random spot on the map. Press **A** next to her to talk. It can be switched off with "Lilo the cat" under "Minimap and game options" in the Royale menu.

## Latest changes (real assets, weather, scenery)
- **Hotbar and loot icons** are the game's own item icons wherever the game has one (swords, bow, bombs, bottles, tunics, masks...). Songs, rupee piles and a few others keep a drawn fallback.
- **Weapons in use:** every action (slash, shot, throw, spell, ocarina, potion) plays the game's own animation once from its first frame; sword slashes cycle through the game's four swings. Arrows, bombs, bombchus and the boomerang in flight are the game's own models; elemental arrows leave sparks.
- **Other players move and sound like Link.** Every action is the game's own animation chain (a slash and its recovery, a potion opened, drunk and finished, a spell gathered, cast and ended), with one-handed swords, Biggoron's Sword and the hammer each swinging their own way. Walking and running play at the speed the player really moves, the lock-on footwork matches the weapon (bow, hookshot, boomerang, bombs), and someone left standing does Link's idle fidgets, or doubles over panting when nearly dead. You hear them too, from where they stand: footsteps that match the ground, the swish and shout of a swing, the bow string, rolls and landings, the shield, chests opening, spells, a cry when they go down, and the real ocarina tune when somebody near you plays a song.
- **Your own moves reach everyone.** Side hops, back flips, jump slashes, spin attacks, raising your shield, jumping and the Z-target footwork are read straight off your Link, so other players see exactly what you did. Side hops and back flips dodge like a roll does. A jump slash hits half again as hard, and a spin attack hits everyone around you (both take a little longer to recover from). A raised shield takes most of a blow that comes from in front, and you hear it ring off; light arrows and explosions go straight through, and two-handed weapons leave no hand free for a shield.
- **Bots play like players.** They side hop and back flip out of swings, open with a jump slash, spin when surrounded, raise their shield, jump about, stop to kick a chest open and hold what was inside up over their heads (you do too, for everyone else), cut bushes and break rocks for rupees and ammo, look around when they stop, and sometimes celebrate an elimination with an emote.
- **Allies** are the game's own NPCs (the Kokiri kid, the Zora, the Goron, the Gerudo) with their real skeletons, textures and animations. Free ones wave you over (Zora spreads its arms, the Gerudo claps); hired ones stand ready, walk with a built-in walk cycle and use attack poses (slingshot aim, Zora's arms wide, the Goron's overhead hammer slam, the Gerudo drawing the bow) while carrying the real item model.
- **Weather:** rain and snow are the game's own particle effects; snow builds up as mounds on the ground, then fills in with a blanket once it lies deep, and melts slowly afterwards. Rain washes the snow away and leaves puddles on level ground that ripple as drops land and dry up slowly when it stops.
- **Grass, trees and flowers** are scattered over the field (sway in the wind, change with the season); trunks are solid. Option: "Grass and trees (%)".
- **Rocks and boulders** are solid and you can walk up onto them; the climbing blocks now step up automatically.
- **Cloth physics** has its own on/off tick box; the cap now swings with Link's acceleration and its tip whips.
- **Music:** the Royale menu shows which folder the .wav songs go in and how many were found, with a Rescan button.
- **Glider:** Link hangs from the bar with both hands up.

## Controls (streamlined)
- **B** attack · **A** open / take / hire / talk · **C-Up** jump · **Left stick click** sprint · **Z** lock on (and dive while skydiving)
- **D-pad Right / Left** next / previous weapon · **D-pad Down** health potion · **C-Left** shield potion · **D-pad Up** ability · **C-Right** emote
- Each filled hotbar slot shows the button that uses it. The game's own C-button icons are hidden during a match; the top right shows the match instead
  (players alive, the zone timer, and short timers for whatever is affecting you). The screen is kept quiet on purpose: sounds, the storm haze and the
  red safe-zone arrow do the warning, and the corner pop-ups are kept for things you need to act on. Everything you carry also appears in the pause-menu inventory, and your own
  inventory is put back when the match ends.
- Options: "Lilo follows me around as a pet" (looks only: no effect on the match, only you see her).
