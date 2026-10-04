# Song to Ocarina of Time

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

The Ocarina of Time sound is the game's own samples, so the `.sf2` has to come from your copy of the game (or an Ocarina of Time `.sf2` you
already have). The tool reads any normal SoundFont 2 file (`.sf2`, 16 or 24 bit). Without `--soundfont` it uses a plain built-in voice so you
can hear the transcription, and says so: that is not the game's sound.

## What to expect

Finding notes in a finished recording is the hard part (the computer must un-mix the song). Clear melodies and simple arrangements come out well;
dense, loud music comes out as the right tune and harmony but not a copy. Drums are not handled. Tuning knobs: `--sensitivity`, `--voices`,
`--min-note-ms`.

`python test_song_to_oot.py` checks the whole pipeline on a made-up song and soundfont.
