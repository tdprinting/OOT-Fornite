# Item audit: what is the game's own and what is ours

**Held items are the game's own.** The weapon you carry sits on Link's B button as the real game item (Deku Stick, swords, hammer, bow, elemental arrows, slingshot, boomerang, bombs, bombchus, deku nuts), so the game's own item code takes it out, holds it, swings, shoots or throws it, with its own models, sounds and flying arrows, bombs and boomerangs. The server still decides damage and ammo. Everyone else (players and bots) is drawn the same way: their weapon in hand, their real shield on their back or arm, their boots on their feet and their mask on their face; the bottle in hand while they drink, the ocarina while they play a song, the hookshot while they fire it. Their arrows, bombs, bombchus, boomerangs, slingshot seeds and deku nuts fly as the game draws them. Spells are the game's own Din's Fire, Nayru's Love and Farore's Wind actors on whoever cast them (a remote cast never tints your screen, burns your surroundings or touches your magic meter), and a song you play brings up the game's own ocarina swirl. Blows that ring off a shield show the game's metal spark. Fire, ice and light arrows mark whoever they hit.

Generated from the source. *Model* = the game's own 3D item model (as dropped loot). *Icon* = the game's own icon texture (hotbar, menus). *In hand* = drawn in the character's hand when it is the held weapon. Gameplay numbers (damage, ranges, cooldowns) are the server's own balance tables (shared/balance.h): the match runs on a headless server that cannot run the game's actor code, so the *behaviour* follows the originals (bombs have a fuse and blast, arrows fly and drop, boomerangs return as a throw, potions heal) but the *code* is ours.

| Item | Kind | Model | Icon | In hand | Effect |
|---|---|---|---|---|---|
| Deku Stick | Weapon | real | real | yes | Weak melee |
| Kokiri Sword | Weapon | real | real | yes | Fast melee |
| Master Sword | Weapon | real | real | yes | Strong melee |
| Biggoron's Sword | Weapon | real | real | yes | Heavy melee, long reach |
| Gilded Sword | Weapon | ours (Blender) | drawn | yes (our model) | The strongest sword: longer reach and a harder hit than the Master Sword |
| Megaton Hammer | Weapon | real | real | yes | Slow, huge melee hit |
| Slingshot | Weapon | real | real | yes | Weak, long-range |
| Fairy Bow | Weapon | real | real | yes | Strong, long-range |
| Boomerang | Weapon | real | real | yes | Mid-range, quick |
| Bombs | Weapon | real | real | yes | Thrown, explodes on everyone near the target |
| Bombchus | Weapon | real | real | yes | Long-range explosive |
| Deku Nuts | Weapon | real | real | - | Flash: stuns the target for 2 seconds |
| Fire Arrows | Weapon | real | real | yes | Sets the target on fire |
| Ice Arrows | Weapon | real | real | yes | Freezes the target: it can't act and takes extra damage |
| Light Arrows | Weapon | real | real | yes | Huge damage that ignores shields |
| Deku Shield | Shield | real | real | - | Absorbs a little damage |
| Hylian Shield | Shield | real | real | - | Absorbs a fair amount of damage |
| Mirror Shield | Shield | real | real | - | Absorbs a lot of damage |
| Green Potion | Consumable | real | real | - | Heals 1 heart |
| Red Potion | Consumable | real | real | - | Heals 2 hearts |
| Blue Potion | Consumable | real | real | - | Heals 3 hearts |
| Fairy | Consumable | real | real | - | Revives you once if you would die |
| Lon Lon Milk | Consumable | real | real | - | Heals 1.5 hearts |
| Fish | Consumable | real | real | - | Heals half a heart |
| Blue Fire | Consumable | real | real | - | Heals half a heart and puts out fire |
| Bugs | Consumable | real | real | - | Heals a little and cures fire and stun |
| Poe | Consumable | real | real | - | Take half damage for 6 seconds |
| Small Shield Potion | Consumable | real | real | - | Adds a third of a shield bar, up to half of it |
| Large Shield Potion | Consumable | real | real | - | Adds two thirds of a shield bar, up to all of it |
| Recovery Heart | Instant | real | real | - | Heals 1 heart on the spot |
| Piece of Heart | Instant | real | real | - | Four make a Heart Container |
| Heart Container | Instant | real | real | - | +1 maximum heart and heals it |
| Magic Jar | Instant | real | real | - | Refills your magic and recharges your ability |
| Adult Power | Instant | game rupee/none | drawn | - | Grow into adult Link for a minute: hit harder, take less damage, run faster |
| Din's Fire | Ability | real | real | - | Fire burst around you |
| Farore's Wind | Ability | real | real | - | Mark a spot, then jump back to it |
| Nayru's Love | Ability | real | real | - | Invulnerable for 4 seconds |
| Hookshot | Ability | real | real | yes | Pull the player in front of you to you |
| Longshot | Ability | real | real | yes | Pull from much farther away |
| Lens of Truth | Ability | real | real | - | See every player for 10 seconds |
| Magic Beans | Ability | real | real | - | Heal over time for 10 seconds |
| Fairy Ocarina | Ability | real | real | yes | Plays a random simple song |
| Ocarina of Time | Ability | real | real | yes | Plays a random song from the whole list |
| Zelda's Lullaby | Ability | real | real | yes | Heals 1 heart |
| Epona's Song | Ability | real | real | - | Run faster for 6 seconds |
| Saria's Song | Ability | real | real | - | See every player for 6 seconds |
| Sun's Song | Ability | real | real | - | Stuns everyone close to you |
| Song of Time | Ability | real | real | - | Freezes everyone nearby while you can't be hurt |
| Song of Storms | Ability | real | real | - | Lightning strikes everyone near you |
| Minuet of Forest | Ability | real | real | - | Run faster and heal half a heart |
| Bolero of Fire | Ability | real | real | - | Sets everyone near you on fire |
| Serenade of Water | Ability | real | real | - | Heals 1.5 hearts and puts out fire |
| Nocturne of Shadow | Ability | real | real | - | Vanish and reappear somewhere else |
| Requiem of Spirit | Ability | real | real | - | Stuns and hurts everyone near you |
| Prelude of Light | Ability | real | real | yes | Heals 1 heart and protects you briefly |
| Shockwave Grenade | Ability | real | real | - | Launches you high into the air; no fall damage until you land |
| Kokiri Tunic | Gear | real | real | - | Plain: slightly less damage taken |
| Goron Tunic | Gear | real | real | - | Half damage from fire and explosions |
| Zora Tunic | Gear | real | real | - | Less storm damage |
| Kokiri Boots | Gear | real | real | - | Plain: a little faster |
| Iron Boots | Gear | real | real | - | Can't be stunned or frozen, slower |
| Hover Boots | Gear | real | real | - | Run noticeably faster |
| Goron's Bracelet | Gear | real | real | - | +10% melee damage |
| Silver Gauntlets | Gear | real | real | - | +20% melee damage |
| Golden Gauntlets | Gear | real | real | - | +35% melee damage |
| Keaton Mask | Gear | real | real | - | A little less storm damage |
| Skull Mask | Gear | real | real | - | +10% ranged damage |
| Spooky Mask | Gear | real | real | - | 8% less damage taken |
| Bunny Hood | Gear | real | real | - | Run 20% faster |
| Goron Mask | Gear | real | real | - | Less fire and explosion damage |
| Zora Mask | Gear | real | real | - | Less storm damage |
| Gerudo Mask | Gear | real | real | - | +10% melee damage |
| Mask of Truth | Gear | real | real | - | +5% ranged damage and 5% less damage taken |
| Silver Scale | Gear | real | real | - | A little less storm damage |
| Golden Scale | Gear | real | real | - | Less storm damage |
| Big Quiver | Gear | real | real | - | +15% ranged damage |
| Bullet Bag | Gear | real | real | - | +10% ranged damage |
| Bomb Bag | Gear | real | real | - | +10% ranged damage |
| Forest Medallion | Gear | real | real | - | Run 12% faster |
| Fire Medallion | Gear | real | real | - | 60% less fire damage |
| Water Medallion | Gear | real | real | - | 40% less storm damage |
| Spirit Medallion | Gear | real | real | - | +20% melee damage |
| Shadow Medallion | Gear | real | real | - | +20% ranged damage |
| Light Medallion | Gear | real | real | - | 12% less damage taken |
| Kokiri's Emerald | Gear | real | real | - | 5% less damage taken, a little faster |
| Goron's Ruby | Gear | real | real | - | Half the explosion damage, 40% less fire damage |
| Zora's Sapphire | Gear | real | real | - | 25% less storm damage |
| Triple Slingshot | Weapon | real | real | yes | Fires three seeds at once: close up they all land |
| Giant's Hammer | Weapon | real | real | yes | A huge slow slam that hits everyone around the target |
| Homing Bombchus | Weapon | real | real | yes | Purple bombchus that chase their target down |
| Basic Sword | Weapon | real | real | yes | Your starting sword: weak, but it never runs out |
| Rupees | Instant | game rupee/none | real | - | Money: hire helpers who follow and fight for you |
| Arrows | Instant | real | real | - | Ammo for the bow and the elemental arrows |
| Deku Seeds | Instant | real | real | - | Ammo for the slingshot |
| Bombs (ammo) | Instant | real | real | - | Ammo for thrown bombs |
| Bombchus (ammo) | Instant | real | real | - | Ammo for bombchus |
| Deku Nuts (ammo) | Instant | real | real | - | Ammo for deku nuts |
