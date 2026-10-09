# ChuChu jelly and arena fixes

Status: corrected explicit texture state (one cycle, no palette, authored UVs, perspective correction), animated gentle body UV ripples and travelling glossy vertex highlights. Faces stay readable; petrified dark ChuChus retain their stone appearance. Confirmed hits now blend the jelly toward a brief warm-white flash.

Boss arena encounters now disable practice invincibility, synchronize the menu toggle, and target their player immediately from the safe spawn. Ordinary practice and the optional invincibility toggle are preserved.

Validation: all 17 boss kinds attacked and damaged the player within 30 seconds in their arena regression test; five variant/mode renderer checks passed with full 256x256 uploads and changing UVs/highlights; mod layout passed. Windows engine syntax check passed against the local engine source. Device appearance and APK playtesting remain to be checked.

Files: RoyaleChuChu.h, features/Lobby.inc, server/match.h, boss_arena_tests.cpp, check_renderer.cpp.

Damage feedback: confirmed nearby hits emit actor-positioned impacts (including bosses) and sword hits add a slice sound, throttled to avoid rapid audio stacking. A small white contact mark improves readability for players/bots/bosses. ChuChu impacts use droplets. Local attacker position now directs the particle spray correctly. Existing self damage cues and hit-feedback options remain.
