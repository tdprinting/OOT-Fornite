# How to get and install OOT Royale (no building needed)

GitHub builds the game for you automatically. You download the finished files from your repo's **Actions** page.

> This is an early test build. **Nobody has launched it yet**, so expect bugs. Use a throwaway save file.

## You will need
- A GitHub login that can open the `tdprinting/OOT-Fornite` repo (the files are private to you).
- **Your own copy of the Ocarina of Time ROM file.** The game asks for it the first time you start it. It is never included in the download.

## Step 1: download the files

1. Open <https://github.com/tdprinting/OOT-Fornite/actions/workflows/game-build.yml>
2. Click the newest run with a **green tick** (the name starts with the latest change, e.g. "CI: package the Windows build...").
3. Scroll to the bottom of the run page to **Artifacts** and click to download:
   - **royale-android-apk** for your Odin 2 Portal or any Android phone
   - **royale-windows** for a Windows PC (appears once the Windows build is green)
4. The download is a `.zip`. Unzip it.

A red cross on a run means that build failed, and there's nothing to download from that platform for that run. Tell me and I'll fix it.

## Android (Odin 2 Portal)

1. Copy the unzipped `.apk` file to the device (USB cable, or a cloud drive).
2. Open it from the Files app and allow **Install unknown apps** when asked.
3. Open the app and **allow all file permissions**.
4. When asked, answer **Yes** to generating the data file, **Yes** to looking for a ROM, and pick your Ocarina of Time ROM file. Wait for the extraction to finish.
5. Press **Back / Select / -** on the controller to open the menu.

## Windows

1. Unzip the `royale-windows` download somewhere permanent (for example `C:\Games\OOT-Royale`).
2. Run `soh.exe`. Allow it through Windows Firewall if asked (needed for multiplayer).
3. The first launch asks for your ROM and extracts the game files.
4. Press **F1** to open the menu.

## Playing a match

1. Start the game, load a save, and walk to **Hyrule Field** (leave the village or the castle town and go out into the open field).
2. Open the menu, go to **Enhancements**, then **OOT Royale**, and open the window.
3. **To host:** type a name and press **Host**. Tell your friends your IP address and the port number shown (7777).
   - Same Wi-Fi: use the device's local IP (Settings, Wi-Fi, your network).
   - Over the internet: use a free VPN like Tailscale on every device (easiest), or forward UDP port 7777 on your router.
4. **To join:** type the host's IP address and press **Join**.
5. The host presses **Start match**. Empty spots fill with bots up to 32 players.

### If something goes wrong
- Nothing happens on Join: check both devices are in Hyrule Field, on the same network or VPN, and the firewall allows the game.
- The window says "Host runs a different version": everyone must install the same build.
- Crashes or odd behavior: tell me what you were doing. This build has never been run, so those reports are exactly what I need.

## What works in this build, and what doesn't yet
- Works (compiled and unit-tested, but not played): hosting, joining, up to 32 players with bots, a storm that shrinks, server-controlled health, other players shown as Link models.
- Not in yet: attacking other players, picking up loot, weapons shown in hands, a proper scoreboard, watching after you are eliminated.
- The size of Hyrule Field is a guess. The window has a checkbox to show your position so we can measure it.
