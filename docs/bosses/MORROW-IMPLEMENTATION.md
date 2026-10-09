# Morrow implementation

Original Astra design and four-side art: [MORROW-DESIGN.md](MORROW-DESIGN.md). Sol authored the procedural model and initial server attacks; primary agent completed renderer, audio, integration and checks after Sol reached its usage limit.

Morrow is selectable in the miniboss test arena and can replace one normal match guard (25% selection per candidate until selected, maximum one per match). Appended Hollowbell ID preserves prior IDs; protocol 35 requires updated peers. Server owns sweep, Toll Road, Third Toll, half-health speed/order changes, recovery windows and four-chest reward. Pending damage and warnings cancel on death/leash return.

Editable model and portable animated GLB: assets/bosses/morrow. Nine rigid animation clips; game uses lightweight procedural part animation with vertex colors instead of texture/cloth simulation. Source: tools/bosses/build_morrow.py. Approximately 3,000 triangles. Game renderer: mod/Royale/RoyaleMorrow.h. Original deterministic sounds: six mono PCM16 44.1kHz WAVs under assets/bosses/morrow/audio, embedded via generated shared/morrow_sounds.h. Rebuild with python tools/bosses/build_morrow_audio.py. Two mixer voices keep impacts separate from warnings.

Verification: focused combat/model/audio regression, server/net/boss arena tests, display-list renderer harness against Shipwright macros, layout and whitespace checks. Full engine compile and Odin visuals/audio/performance remain device/CI checks; this local environment has no configured full engine compiler build. No plant boss included.
