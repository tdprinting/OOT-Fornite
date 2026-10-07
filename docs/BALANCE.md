# Damage balance

Everyone starts a match with 7 hearts. The numbers below are measured against that. To tune, change the dials in `shared/balance.h`:

| Dial | Value | What it scales |
|---|---|---|
| `kPlayerDamageScale` | 0.85 | Anything a player or bot does to another player: weapons, splash, burns, spells, helpers, running people over |
| `kBossDamageScale` | 0.75 | Mini bosses and the major boss (blows, breath, marked blasts) |
| `kHazardDamageScale` | 0.75 | Nobody's fault: lightning, cart crashes and wreck blasts nobody caused, Lilo's cloud |
| `kStormPhases` | table | Storm damage per phase (not scaled by the dials above) |

The dials are applied once, in `Match::Damage`, after gear, shields and other multipliers.

## Storm

Time to lose all 7 hearts while standing outside the safe zone on a big map (small maps hurt a little less).

| Phase | Before (hearts/s) | Time to die | Now (hearts/s) | Time to die | Shrink time before → now |
|---|---|---|---|---|---|
| 1 | 0.5 | 14 s | 0.2 | 35 s | 55 → 65 s |
| 2 | 1.0 | 7 s | 0.4 | 17.5 s | 45 → 55 s |
| 3 | 1.5 | 4.7 s | 0.7 | 10 s | 40 → 45 s |
| 4 | 2.0 | 3.5 s | 1.0 | 7 s | 35 → 38 s |
| 5 | 3.0 | 2.3 s | 1.5 | 4.7 s | 30 → 32 s |
| 6 | 5.0 | 1.4 s | 2.0 | 3.5 s | 22 → 25 s |

The whole storm now runs about 7.4 minutes instead of 6.8, so the major boss (halfway) arrives a little later too.

## Weapons

Hits to take a full 7 hearts with no shield, Common / Legendary. Time is the fastest kill at Common (cooldown between hits).

| Weapon | Before | Now | Fastest kill now |
|---|---|---|---|
| Deku Stick | 14 / 8 | 17 / 10 | 11.2 s |
| Basic Sword | 13 / 8 | 15 / 9 | 9.1 s |
| Kokiri Sword | 9 / 5 | 11 / 6 | 6.0 s |
| Master Sword | 5 / 3 | 6 / 4 | 3.0 s |
| Biggoron's Sword | 4 / 2 | 5 / 3 | 4.4 s |
| Megaton Hammer | 3 / 2 | 4 / 2 | 3.9 s |
| Giant's Hammer | 3 / 2 | 3 / 2 | 3.4 s (base 3.2 → 2.8) |
| Slingshot | 14 / 8 | 17 / 10 | 12.8 s |
| Fairy Bow | 7 / 4 | 9 / 5 | 8.0 s |
| Bombs | 4 / 3 | 5 / 3 | 8.0 s |
| Bombchus | 5 / 3 | 6 / 4 | 12.5 s |
| Fire Arrows (plus burn) | 8 / 5 | 10 / 6 | 10.8 s |
| Light Arrows | 4 / 3 | 6 / 3 | 9.0 s (base 1.8 → 1.6) |

## Bosses and hazards

Mini boss swing wind-up is 0.7 s (was 0.55 s) so there is time to roll or raise a shield, swings come 0.2 s less often, and they rest a second longer between special moves. Walking speed (player run = 100, sprint = 135): Stalfos 72 → 66, Magma Dodongo 58 → 55, White Wolfos 88 → 80, Lizalfos 76 → 70, Big Octo 70 → 66, Dead Hand 50 → 48, Iron Knuckle 55 → 52. Major bosses: attack gap 3.0 → 3.4 s and speeds Volvagia 150 → 135, Morpha 120 → 110, Phantom Ganon 160 → 140, Bongo Bongo 130 → 120, Twinrova 190 → 160. A heavy hit (8% of its health, e.g. a Master Sword swing on a Stalfos) knocks a mini boss out of a wind-up.

Every boss hit is 75% of what it was: a Stalfos swing goes from 0.8 to 0.6 hearts, a White Wolfos from 1.2 to 0.9, the major boss's fire blast from 1.0 to 0.75 and its breath from 0.6 to 0.45 hearts a second. Lightning goes from 0.75 to 0.56 hearts, a wrecked cart's blast nobody caused from 1.5 to 1.1.

## Bot matches (`royale_balance 8 normal 0 24`)

| | Before | Now |
|---|---|---|
| Match length | 349 s | 404 s |
| Alive at 1:00 / 3:00 (of 23) | 20.6 / 12.9 | 20.1 / 12.4 |
| Storm eliminations per match | 4.2 | 3.5 |

## One set of hit rules

A boss's swing or charge and a hired helper's strike now go through `Match::Blow`, the same rules a player's weapon follows: rolling dodges it, a shield's own reduction comes off, and a raised shield facing it blocks most of the rest. Rolling also dodges marked blasts (not lightning). Breath, explosions and magic still ignore shields, as they do for players.

## Z-targeting

Mini bosses, major bosses (while visible) and hireable helpers can be Z-targeted like the game's own enemies and NPCs (red reticle for bosses, white for helpers). While locked on a player or boss, your blow goes to that target when it is in reach, whichever way the stick points.
