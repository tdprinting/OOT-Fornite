# Your own Peter voice clips (personal use)

The repo ships only original synthesised audio. To hear other clips in your own game, put 16-bit PCM `.wav` files in the
`voices/peter/` folder inside the game's data folder (next to `music/`) on your device. Any rate or channel count works; they
are converted to mono 22050 Hz when first used. Do not commit them: `voices/` and `assets/npcs/peter/custom/` are git-ignored.

One-shot cues: `greet.wav` (laugh near a free Peter), `hire.wav` (first hire), `cheer.wav` (later hires), `attack.wav` (haymaker),
`chicken.wav`, `belly.wav`, `kick.wav`, `hurt.wav`.

Phone call: `call.wav` replaces the whole 26.6 s call (ring at 0 s; subtitles keep the timing below). Or replace single lines; each
starts at its line's time and the built-in line under it is muted:

| File | Who | Line |
|---|---|---|
| `line01.wav` or `hey_peter.wav` | Joe | Hey Peter! (2.3-3.3 s) |
| `line02.wav` | Peter | Hey Joe! What's goin' on? (3.5-5.0 s) |
| `line03.wav` | Joe | There's a giant storm wall closing in on Hyrule Field. You safe? (5.2-7.6 s) |
| `line04.wav` | Peter | Relax, Joe. Some kid in a green dress paid me rupees to punch stuff. (7.8-10.4 s) |
| `line05.wav` | Joe | Rupees? Peter, that's not real money. (10.6-12.6 s) |
| `line06.wav` | Peter | Joe, I just opened a treasure chest and there was a fish in it. Nothing's real anymore. (12.8-15.6 s) |
| `line07.wav` | Joe | How'd you even end up in there? (15.8-17.4 s) |
| `line08.wav` | Peter | Cynthia, Joe. She said 'put Peter in the game' as a joke. Now I'm a real game asset! (17.6-21.0 s) |
| `line09.wav` | Joe | ...Well, tell Cynthia I said hi. (21.2-23.0 s) |
| `line10.wav` | Peter | Heh heh heh heh. Bye, Joe. (23.2-25.4 s) |

A clip longer than its slot keeps playing; keep each close to its time window so the subtitles line up.
