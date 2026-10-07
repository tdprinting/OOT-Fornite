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

Every boss hit is 75% of what it was: a Stalfos swing goes from 0.8 to 0.6 hearts, a White Wolfos from 1.2 to 0.9, the major boss's fire blast from 1.0 to 0.75 and its breath from 0.6 to 0.45 hearts a second. Lightning goes from 0.75 to 0.56 hearts, a wrecked cart's blast nobody caused from 1.5 to 1.1.

## Bot matches (`royale_balance 8 normal 0 24`)

| | Before | Now |
|---|---|---|
| Match length | 349 s | 404 s |
| Alive at 1:00 / 3:00 (of 23) | 20.6 / 12.9 | 20.1 / 12.4 |
| Storm eliminations per match | 4.2 | 3.5 |
