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
- **The item bar** at the bottom works like Fortnite's hotbar: three weapon slots (the one in your hand is highlighted), then shield, potions and ability. **D-pad Left** switches to the next weapon. On a touch screen you can tap a slot. A big coloured banner shows each pickup, and the menu keeps a colour-coded list of recent pickups.
- **B** attacks with the weapon in hand (its damage and range come from the item and its rarity, not from your sword). Some weapons add effects: Fire Arrows burn, Ice Arrows freeze, Deku Nuts stun, Light Arrows ignore shields, Bombs hurt everyone near the target.
- **D-pad Down** drinks a potion (the best one for how hurt you are). A Fairy in your bag is never drunk: it revives you once if you would die.
- **D-pad Up** uses your ability, then it recharges. The slot shows the countdown.
- Gear works on its own once you pick it up. Heart Pieces and Heart Containers raise your maximum hearts.
- **Other players** wear a nameplate with their name, hearts left and weapon, colour coded by rarity, so you can size them up. The map (bottom left) shows the field, the safe zone, chests, other players and you.
- **The storm** is a wall of purple rain standing on the edge of the safe zone. Outside it the screen goes dark and rainy with lightning, and it hurts. The map shows it in purple. An arrow top right points to the safe zone.
- **Towns.** The field has up to twelve named points of interest with silly rhyming names (Deku Dew Zoo, Goron Groove Lagoon, Zora Snore Shore, Navi Gravy Bay and more), laid out like a battle royale map: a big landmark in the middle (Hylian Billion Pavilion), a ring of towns around it and a wider ring near the edge. Each has a walled stone building with a roof and a door, a small cave (a horseshoe of boulders) and some ruins, and most of the chests are inside them. A few are scattered between towns. Town names float over each place (big enough to read while you skydive), show on the map, and a message tells you when you walk in.
- The open ground between towns has rocks, boulders and bushes to hide behind. The same ones appear for everyone, and bots path around the solid ones and through the doors.
- **Mini bosses.** Up to five golems (Stone Moan, Lava Java, Frost Lost) guard the caves, bigger and tougher in that order. They notice you at a distance, chase you slower than you run, smash you for a heart or more and give up if you get far away. Hit them with **B** like any player (you can hit them from a little further off, because they are big). A health bar floats over each, and they show as purple diamonds on the map. When one falls it drops three to five Epic and Legendary chests around it, you score 400 points for the last hit, and bots go after them too if they are strong enough (weak ones keep away).
- **Falling limp.** When a player is eliminated their body is thrown back, bounces, slides and settles in Link's knocked-down pose, then fades after a while. Yours too.
- **Jumping.** **C-Up** jumps. If there is a ledge about knee to chest high right in front of you, the jump pulls you up onto it.
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
- Everything above is written and compiled, and the server side is unit-tested, but **none of it has been played**. Expect rough edges.
- The map size is measured automatically when the host presses Start (the host is taken to Hyrule Field first), and loot, spawn points and the storm are kept on ground that exists. Bots walk in straight lines, so they can walk through walls and water.
- Not in yet: shields and gear are not drawn on other players, there is no minimap, and the item list is long (82 items), so expect balance problems. Bombs and arrows act as instant hits at range; there is no flying projectile yet.
- Bots now find their way around obstacles in Hyrule Field if the map probe found them, strafe and dodge in fights, flee losing fights, use abilities and hunt in the endgame. They still can't see the difference between a ledge and a cliff, and they see through walls.
