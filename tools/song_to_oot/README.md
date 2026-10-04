# Song to Ocarina of Time

**No Python?** Open `tools/song-to-oot.html` in a browser instead: it does everything below (ROM to soundfont, hearing the instruments,
finding the notes, saving .wav/.mp3/.mid/.sf2) inside the page, and nothing is uploaded. The page also picks the game soundfont that best
fits the song, adds drums when the song has them, and finds notes with Spotify's open-source Basic Pitch model (Apache-2.0, built into the
page; TensorFlow.js loads from a CDN the first time). `node tools/test_song_to_oot_html.js` tests it.

Turns any song into a version played with the Ocarina of Time instruments: it works out the notes, then plays them again with a SoundFont.
You get a `.wav`, an `.mp3`, and the `.mid` of the notes it found.

## Use it

1. Install Python 3.8+ and ffmpeg, then once: `pip install numpy`
2. Run it:

```
python song_to_oot.py mysong.mp3 --soundfont oot.sf2
```

No arguments opens a small window to pick the song and the soundfont instead.

Useful options: `--list-presets` (shows the instruments in the soundfont), `--melody ocarina --harmony harp --bass bass` (pick instruments by name,
number or bank:program), `--reverb 0.4`, `--sensitivity 0.1` (find quieter notes), `--voices 6` (more notes at once).
A `.mid` file as the song is rebuilt exactly with no guessing.

## The soundfont

The Ocarina of Time sound is the game's own samples, so the soundfont has to come from your copy of the game. Give your ROM and it is made for you:

```
python song_to_oot.py mysong.mp3 --soundfont "Ocarina of Time.z64"
```

That writes `oot.sf2` next to the output once and reuses it. To make it on its own (for another synth or a DAW too):

```
python oot_soundfont.py "Ocarina of Time.z64" -o oot.sf2
python oot_soundfont.py "Ocarina of Time.z64" --list
```

It reads `.z64`, `.v64` and `.n64` ROMs, compressed or not, and finds the audio tables itself. Only the soundfonts the game's music uses are
kept (the game's own song-to-soundfont table says which); `--all` also keeps the sound effect ones. Every game soundfont becomes a bank, so preset
`3:5` is font 3, instrument 5, and each font's drum kit is `128:<font>`. The ROM has no instrument names, so presets are called `OoT f03 i05`:
pick them with `--melody 3:5` and friends after a look at `--list-presets`. Samples, loops, tuning and key splits are exact; the game's volume
envelopes are approximated with a SoundFont's attack, decay, sustain and release.

The ROM and the `.sf2` made from it are yours alone: never commit or share them (`.gitignore` keeps them out of this repository).

Any other SoundFont 2 file (`.sf2`, 16 or 24 bit) works too. Without `--soundfont` it uses a plain built-in voice so you can hear the
transcription, and says so: that is not the game's sound.

## What to expect

Finding notes in a finished recording is the hard part (the computer must un-mix the song). Clear melodies and simple arrangements come out well;
dense, loud music comes out as the right tune and harmony but not a copy. Drums are not handled. Tuning knobs: `--sensitivity`, `--voices`,
`--min-note-ms`.

`python test_song_to_oot.py` checks the whole pipeline on a made-up song and soundfont, and `python test_oot_soundfont.py` checks the
extractor on a made-up ROM (no game data needed).
