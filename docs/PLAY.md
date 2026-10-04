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

1. Copy the unzipped `.apk` file to the device (USB cable, or a cloud drive).
2. Open it from the Files app and allow **Install unknown apps** when asked.
3. Open the app and **allow all file permissions**.
4. When asked, answer **Yes** to generating the data file, **Yes** to looking for a ROM, and pick your Ocarina of Time ROM file. Wait for the extraction to finish.
5. Press **Back / Select / -** on the controller to open the menu.

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
6. The host picks **Bot difficulty** (Easy, Normal or Hard) and presses **Start match**. Empty spots fill with bots up to 32 players. After a 10 second countdown everyone is moved to Hyrule Field automatically.

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
- **Walk over items** to pick them up. Each one shows its name above it in its rarity color (grey Common, green Uncommon, blue Rare, purple Epic, gold Legendary) and is drawn as a rupee of that color.
- **B** attacks with the weapon you picked up (its damage and range come from the item and its rarity, not from your sword). Some weapons add effects: Fire Arrows burn, Ice Arrows freeze, Deku Nuts stun, Light Arrows ignore shields, Bombs hurt everyone near the target. It hits the nearest player in front of you. Z-target another player like an enemy to line up a duel.
- **D-pad Down** drinks a potion (the best one for how hurt you are). A Fairy in your bag is never drunk: it revives you once if you would die.
- **D-pad Up** uses your ability (Din's Fire, Nayru's Love, Hookshot, Farore's Wind, a song, and more), then it recharges. The HUD shows the countdown.
- Gear (tunics, boots, masks and so on) works on its own once you pick it up. Heart Pieces and Heart Containers raise your maximum hearts.
- The top-left of the screen shows how many are alive, the storm timer, what you are holding, your ability, your gear and any effects on you. An arrow points to the safe zone.
- If you are eliminated you become an invisible spectator and can keep walking around while the match finishes.

## What works in this build, and what doesn't yet
- Everything above is written and compiled, and the server side is unit-tested, but **none of it has been played**. Expect rough edges.
- The map size is measured automatically when the host presses Start (the host is taken to Hyrule Field first), and loot, spawn points and the storm are kept on ground that exists. Bots walk in straight lines, so they can walk through walls and water.
- Not in yet: shields and gear are not drawn on other players, there is no minimap, and the item list is long (82 items), so expect balance problems. Bombs and arrows act as instant hits at range; there is no flying projectile yet.
- Bots now find their way around obstacles in Hyrule Field if the map probe found them, strafe and dodge in fights, flee losing fights, use abilities and hunt in the endgame. They still can't see the difference between a ledge and a cliff, and they see through walls.
