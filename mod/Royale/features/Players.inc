// ---- puppets (other players drawn as Link) ---------------------------------------------------------------------------

uint16_t gSpawningPuppet = 0;                              // player id being spawned right now, 0 when none
std::unordered_map<const Actor*, uint16_t> gPuppetOf;      // actor -> player id
std::unordered_map<uint16_t, Actor*> gActorOf;             // player id -> actor
std::unordered_map<uint16_t, royale::PuppetState> gState;  // latest interpolated state per player id
std::unordered_map<const Actor*, const void*> gPlaying;    // animation each puppet is currently playing
std::unordered_map<const Actor*, std::string> gPlate;      // the nameplate text each puppet currently shows

// Floor height under (x, z), or `fallback` if the ray finds nothing.
float GroundY(PlayState* play, float x, float z, float fallback) {
    CollisionPoly poly;
    Vec3f pos = { x, 4000.0f, z };
    float y = BgCheck_AnyRaycastFloor1(&play->colCtx, &poly, &pos);
    return y > BGCHECK_Y_MIN + 1.0f ? y : fallback;
}

// Where a bot is drawn. The server moves bots in x and z and sends their height above the scene's own floor: 0 on the ground, the top of a
// climbing block or boulder they stand on, or how high they still are in the skydive. (The scene floor without our scenery, because the
// scenery near you is solid and would otherwise be counted twice.)
float BotY(PlayState* play, float x, float z, float lift) {
    float y = 0;
    if (!RawFloorAt(x, z, &y)) y = GroundY(play, x, z, lift);
    return y + lift;
}

// How a puppet holds the weapon the server says it has. Player_SetModelGroup and Player_Draw read the *local* equipped sword,
// so the matching item is swapped in around those calls.
struct Look {
    s32 modelGroup;
    s8 itemAction;
    u8 buttonItem;
};
Look LookFor(royale::ItemId weapon) {
    using royale::ItemId;
    switch (weapon) {
        case ItemId::BasicSword:
        case ItemId::KokiriSword: return { PLAYER_MODELGROUP_SWORD_AND_SHIELD, PLAYER_IA_SWORD_KOKIRI, ITEM_SWORD_KOKIRI };
        case ItemId::MasterSword:
        case ItemId::GildedSword: return { PLAYER_MODELGROUP_SWORD_AND_SHIELD, PLAYER_IA_SWORD_MASTER, ITEM_SWORD_MASTER };   // (the Gilded Sword is the Master Sword's moves with a model of its own: see OnPlayerCustomSword)
        case ItemId::BiggoronSword: return { PLAYER_MODELGROUP_BGS, PLAYER_IA_SWORD_BIGGORON, ITEM_SWORD_BGS };
        case ItemId::GiantsHammer:
        case ItemId::MegatonHammer: return { PLAYER_MODELGROUP_HAMMER, PLAYER_IA_HAMMER, ITEM_HAMMER };
        case ItemId::FairyBow: return { PLAYER_MODELGROUP_BOW_SLINGSHOT, PLAYER_IA_BOW, ITEM_BOW };
        case ItemId::TripleSlingshot:
        case ItemId::Slingshot: return { PLAYER_MODELGROUP_BOW_SLINGSHOT, PLAYER_IA_SLINGSHOT, ITEM_SLINGSHOT };
        case ItemId::Boomerang: return { PLAYER_MODELGROUP_BOOMERANG, PLAYER_IA_BOOMERANG, ITEM_BOOMERANG };
        case ItemId::Hookshot: return { PLAYER_MODELGROUP_HOOKSHOT, PLAYER_IA_HOOKSHOT, ITEM_HOOKSHOT };
        case ItemId::ShockwaveGrenade: return { PLAYER_MODELGROUP_EXPLOSIVES, PLAYER_IA_BOMB, ITEM_BOMB };   // thrown like a bomb
        case ItemId::Longshot: return { PLAYER_MODELGROUP_HOOKSHOT, PLAYER_IA_LONGSHOT, ITEM_LONGSHOT };
        case ItemId::FireArrows: case ItemId::IceArrows: case ItemId::LightArrows:
            return { PLAYER_MODELGROUP_BOW_SLINGSHOT, PLAYER_IA_BOW, ITEM_BOW };   // the elemental arrows are loosed from the real bow
        case ItemId::DekuStick: return { PLAYER_MODELGROUP_10, PLAYER_IA_DEKU_STICK, ITEM_STICK };
        case ItemId::Bombs: case ItemId::Bombchus: case ItemId::HomingBombchus:
            return { PLAYER_MODELGROUP_EXPLOSIVES, PLAYER_IA_BOMB, ITEM_BOMB };
        case ItemId::FairyOcarina: return { PLAYER_MODELGROUP_OCARINA, PLAYER_IA_OCARINA_FAIRY, ITEM_OCARINA_FAIRY };
        default:
            if (weapon == ItemId::OcarinaOfTime || (weapon >= ItemId::ZeldasLullaby && weapon <= ItemId::PreludeOfLight))
                return { PLAYER_MODELGROUP_OOT, PLAYER_IA_OCARINA_OF_TIME, ITEM_OCARINA_TIME };
            return { PLAYER_MODELGROUP_DEFAULT, PLAYER_IA_NONE, ITEM_NONE };
    }
}
// The Deku Stick is drawn scaled by Player::unk_85C (its length), which the game sets to 1 only when it takes the stick out
// (Player_InitDekuStickIA). Anywhere the stick is put in the hand by setting the item action directly, that length is still 0 and the stick is invisible.
void ShowStickLength(Player* player) {
    if (player->heldItemAction == PLAYER_IA_DEKU_STICK && player->unk_85C == 0.0f) player->unk_85C = 1.0f;
}
// A bottle in the hand, coloured by what is in it, as Link holds one to drink.
Look BottleLook(royale::ItemId contents) {
    using royale::ItemId;
    s8 action = PLAYER_IA_BOTTLE_POTION_RED;
    switch (contents) {
        case ItemId::GreenPotion: action = PLAYER_IA_BOTTLE_POTION_GREEN; break;
        case ItemId::BluePotion: case ItemId::SmallShieldPotion: case ItemId::LargeShieldPotion: action = PLAYER_IA_BOTTLE_POTION_BLUE; break;
        case ItemId::Milk: action = PLAYER_IA_BOTTLE_MILK_FULL; break;
        case ItemId::Fish: action = PLAYER_IA_BOTTLE_FISH; break;
        case ItemId::BlueFire: action = PLAYER_IA_BOTTLE_FIRE; break;
        case ItemId::Bug: action = PLAYER_IA_BOTTLE_BUG; break;
        case ItemId::Poe: action = PLAYER_IA_BOTTLE_POE; break;
        case ItemId::Fairy: action = PLAYER_IA_BOTTLE_FAIRY; break;
        default: break;
    }
    return { PLAYER_MODELGROUP_BOTTLE, action, ITEM_BOTTLE };
}
// The shield, boots and mask a player has, as the game's own values for drawing them on Link.
s32 PlayerShieldFor(royale::ItemId id) {
    switch (id) {
        case royale::ItemId::DekuShield: return PLAYER_SHIELD_DEKU;
        case royale::ItemId::HylianShield: return PLAYER_SHIELD_HYLIAN;
        case royale::ItemId::MirrorShield: return PLAYER_SHIELD_MIRROR;
        default: return PLAYER_SHIELD_NONE;
    }
}
s32 PlayerBootsFor(royale::ItemId id) {
    switch (id) {
        case royale::ItemId::IronBoots: return PLAYER_BOOTS_IRON;
        case royale::ItemId::HoverBoots: return PLAYER_BOOTS_HOVER;
        default: return PLAYER_BOOTS_KOKIRI;
    }
}
u8 PlayerMaskFor(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::KeatonMask: return PLAYER_MASK_KEATON;
        case ItemId::SkullMask: return PLAYER_MASK_SKULL;
        case ItemId::SpookyMask: return PLAYER_MASK_SPOOKY;
        case ItemId::BunnyHood: return PLAYER_MASK_BUNNY;
        case ItemId::GoronMask: return PLAYER_MASK_GORON;
        case ItemId::ZoraMask: return PLAYER_MASK_ZORA;
        case ItemId::GerudoMask: return PLAYER_MASK_GERUDO;
        case ItemId::MaskOfTruth: return PLAYER_MASK_TRUTH;
        default: return PLAYER_MASK_NONE;
    }
}
const royale::PuppetState* StateOf(const Actor* actor) {
    auto id = gPuppetOf.find(actor);
    if (id == gPuppetOf.end()) return nullptr;
    auto st = gState.find(id->second);
    return st == gState.end() ? nullptr : &st->second;
}

// The chicken dance, made from scratch: Link's standing animation with his limbs posed over it, in four one-second bars that repeat -
// beak (hands snapping open and shut in front), wings (elbows out, flapping), tail (shaking the hips while bobbing down) and a clap.
// `t` is seconds. Limb axes were worked out from the skeleton's layout, not from seeing it, so the amplitudes are modest.
float ChickenDanceBob(float t) {
    const float bar = std::fmod(t, 4.0f);
    return bar >= 2.0f && bar < 3.0f ? -std::fabs(std::sin(t * 12.0f)) * 9.0f : 0.0f; // squatting through the tail wiggle
}

void ApplyChickenDance(Player* p, float t) {
    constexpr float deg = 32768.0f / 180.0f;
    Vec3s* j = p->skelAnime.jointTable;
    const int bar = static_cast<int>(std::fmod(t, 4.0f));
    const float beat = t * 6.2831853f * 2.5f; // 2.5 flaps or snaps a second
    const float s = std::sin(beat);
    auto add = [&](int limb, float rx, float ry, float rz) {
        j[limb].x = static_cast<s16>(j[limb].x + rx * deg);
        j[limb].y = static_cast<s16>(j[limb].y + ry * deg);
        j[limb].z = static_cast<s16>(j[limb].z + rz * deg);
    };
    switch (bar) {
        case 0: { // beak: forearms up and forward, snapping
            const float snap = s > 0 ? 1.0f : 0.0f;
            add(PLAYER_LIMB_L_SHOULDER, 0, 0, 50.0f);
            add(PLAYER_LIMB_R_SHOULDER, 0, 0, -50.0f);
            add(PLAYER_LIMB_L_FOREARM, 0, 0, 55.0f - 40.0f * snap);
            add(PLAYER_LIMB_R_FOREARM, 0, 0, -55.0f + 40.0f * snap);
            break;
        }
        case 1: { // wings: arms out, elbows flapping
            add(PLAYER_LIMB_L_SHOULDER, 0, 0, 70.0f + 10.0f * s);
            add(PLAYER_LIMB_R_SHOULDER, 0, 0, -70.0f - 10.0f * s);
            add(PLAYER_LIMB_L_FOREARM, 0, 0, 40.0f * s);
            add(PLAYER_LIMB_R_FOREARM, 0, 0, -40.0f * s);
            break;
        }
        case 2: { // tail: hands tucked at the back, hips shaking
            add(PLAYER_LIMB_L_SHOULDER, 0, 0, 25.0f);
            add(PLAYER_LIMB_R_SHOULDER, 0, 0, -25.0f);
            add(PLAYER_LIMB_L_FOREARM, 0, 0, 70.0f);
            add(PLAYER_LIMB_R_FOREARM, 0, 0, -70.0f);
            add(PLAYER_LIMB_WAIST, 0, 28.0f * std::sin(t * 6.2831853f * 4.0f), 0);
            break;
        }
        default: { // clap: arms swinging in together
            const float clap = 0.5f + 0.5f * s;
            add(PLAYER_LIMB_L_SHOULDER, 0, 0, 20.0f + 35.0f * (1.0f - clap));
            add(PLAYER_LIMB_R_SHOULDER, 0, 0, -20.0f - 35.0f * (1.0f - clap));
            add(PLAYER_LIMB_L_FOREARM, 0, 0, 30.0f);
            add(PLAYER_LIMB_R_FOREARM, 0, 0, -30.0f);
            break;
        }
    }
}

std::unordered_map<uint16_t, int> gSwingFrames; // player id -> frames of slash animation left (set when they are seen hurting someone)
// A recoil laid over whatever a bot or mini boss is doing: it rocks away from the blow and settles back, without touching its animation.
struct HitRecoil { float amp = 0, age = 10.0f, dx = 0, dz = 0; };   // amp in radians at the peak; (dx, dz) the direction it is pushed
std::unordered_map<uint16_t, HitRecoil> gHitRecoil;
// The pitch (+ forward) and roll the recoil `r` gives something facing `yaw` (radians) right now.
void RecoilTilt(const HitRecoil& r, float yaw, float* pitch, float* roll) {
    *pitch = *roll = 0.0f;
    if (r.age > 0.5f || r.amp <= 0.0f) return;
    const float k = r.amp * std::exp(-r.age * 9.0f) * std::cos(r.age * 20.0f);   // a quick rock away, a small swing back, then still
    *pitch = k * (r.dx * std::sin(yaw) + r.dz * std::cos(yaw));
    *roll = -k * (r.dx * std::cos(yaw) - r.dz * std::sin(yaw));
}
std::unordered_map<uint16_t, int> gFlinchFrames; // player id -> frames of a flinch left (set when they are seen getting hurt)
std::unordered_map<uint16_t, royale::ItemId> gLastAbility;   // player id -> the ability they last used (which spell or song a cast or a tune is)
std::unordered_map<uint16_t, double> gLastAbilityAt;         // and when

void SpawnProjectileFrom(royale::ItemId weapon, float x, float y, float z, s16 yaw); // below, with the other custom models

// ---- how other players move: the game's own animations, chained the way the game chains them -----------------------------------

#define RA(n) ((LinkAnimationHeader*)&gPlayerAnim_link_##n)

// How a weapon is held decides which of Link's move sets it uses (one-handed sword, two-handed sword, hammer, bow...).
enum class Grip : uint8_t { OneHand, TwoHand, Hammer, Bow, Hook, Boomerang, Explosive, Ocarina, Bare };
Grip GripOf(royale::ItemId w) {
    using royale::ItemId;
    switch (w) {
        case ItemId::BiggoronSword: return Grip::TwoHand;
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: return Grip::Hammer;
        case ItemId::FairyBow: case ItemId::Slingshot: case ItemId::TripleSlingshot: case ItemId::FireArrows: case ItemId::IceArrows: case ItemId::LightArrows: return Grip::Bow;
        case ItemId::Hookshot: case ItemId::Longshot: return Grip::Hook;
        case ItemId::Boomerang: return Grip::Boomerang;
        case ItemId::Bombs: case ItemId::Bombchus: case ItemId::HomingBombchus: case ItemId::DekuNuts: return Grip::Explosive;
        default: break;
    }
    const Look look = LookFor(w);
    if (look.modelGroup == PLAYER_MODELGROUP_OCARINA || look.modelGroup == PLAYER_MODELGROUP_OOT) return Grip::Ocarina;
    if (look.modelGroup == PLAYER_MODELGROUP_SWORD_AND_SHIELD || look.modelGroup == PLAYER_MODELGROUP_10) return Grip::OneHand;
    return Grip::Bare;
}

// An action is up to three of the game's animations in a row: a slash and its recovery, a potion opened, drunk and finished, a spell
// gathered, loosed and ended. The last one loops (walking, holding a stance) or holds its final pose until the player does something else.
struct AnimStep { LinkAnimationHeader* anim; bool loop; };
struct AnimSeq {
    AnimStep step[3] = {};
    int count = 0;
    AnimSeq() = default;
    AnimSeq(LinkAnimationHeader* a, bool loop) { Add(a, loop); }
    AnimSeq& Add(LinkAnimationHeader* a, bool loop = false) { if (count < 3 && a != nullptr) step[count++] = { a, loop }; return *this; }
};

// Sprinting, seen on anyone: the run cycle sped up (Link's own follows his real speed) and small puffs of dust from the heels.
constexpr float kSprintAnimSpeed = 1.45f;
void SprintDust(PlayState* play, Actor* actor, u32 frame) {
    if (frame % 3 != 0) return;
    Vec3f at = actor->world.pos;
    const float back = actor->shape.rot.y * (3.14159265f / 32768.0f);
    at.x -= std::sin(back) * 12.0f;
    at.z -= std::cos(back) * 12.0f;
    Actor_SpawnFloorDustRing(play, actor, &at, 8.0f, 1, 4.0f, 70, 15, true);
}
// Actions are played once, from their first frame, each time one begins (see Puppet_Update).
bool OneShotAnim(uint8_t anim) {
    using royale::Anim;
    switch (static_cast<Anim>(anim)) {
        case Anim::Attack: case Anim::Shoot: case Anim::Throw: case Anim::Drink: case Anim::Play: case Anim::Cast: case Anim::Roll:
        case Anim::JumpSlash: case Anim::SpinAttack: case Anim::HopL: case Anim::HopR: case Anim::Backflip: case Anim::ItemGet:
        case Anim::OpenChest: case Anim::Jump: case Anim::Hurt: case Anim::Dead: case Anim::Guard: return true;
        default: return false;
    }
}

// The game's sword swings in the order a player strings them together: a slash, a slash from the other side, then a finishing blow
// (sometimes a stab). One-handed swords, the two-handed Biggoron's Sword and the hammer each have their own set.
AnimSeq SwingFor(Grip grip, int combo) {
    const int n = combo % 4;
    switch (grip) {
        case Grip::Hammer:
            return n % 2 == 0 ? AnimSeq(RA(hammer_hit), false).Add(RA(hammer_hit_end)) : AnimSeq(RA(hammer_side_hit), false).Add(RA(hammer_side_hit_end));
        case Grip::TwoHand: {
            static LinkAnimationHeader* const hit[4] = { RA(fighter_Lnormal_kiru), RA(fighter_LLside_kiru), RA(fighter_LRside_kiru_finsh), RA(fighter_Lpierce_kiru) };
            static LinkAnimationHeader* const end[4] = { RA(fighter_Lnormal_kiru_end), RA(fighter_LLside_kiru_end), RA(fighter_LRside_kiru_finsh_end), RA(fighter_Lpierce_kiru_end) };
            return AnimSeq(hit[n], false).Add(end[n]);
        }
        default: {
            static LinkAnimationHeader* const hit[4] = { RA(fighter_normal_kiru), RA(fighter_Lside_kiru), RA(fighter_Rside_kiru_finsh), RA(fighter_pierce_kiru) };
            static LinkAnimationHeader* const end[4] = { RA(fighter_normal_kiru_end), RA(fighter_Lside_kiru_end), RA(fighter_Rside_kiru_finsh_end), RA(fighter_pierce_kiru_end) };
            return AnimSeq(hit[n], false).Add(end[n]);
        }
    }
}

// Standing ready with what is in hand, and the lock-on footwork, both as the game shows them for that weapon.
LinkAnimationHeader* StanceFor(Grip grip) {
    switch (grip) {
        case Grip::Bow: return RA(bow_bow_wait);
        case Grip::Hook: return RA(hook_wait);
        case Grip::Boomerang: return RA(boom_throw_waitR);
        case Grip::TwoHand: case Grip::Hammer: return RA(fighter_waitR_long);
        default: return RA(anchor_waitR);
    }
}
LinkAnimationHeader* SideStepFor(Grip grip, bool left) {
    switch (grip) {
        case Grip::Bow: return RA(bow_side_walk);
        case Grip::Hook: return RA(hook_side_walk);
        case Grip::Boomerang: return left ? RA(boom_throw_side_walkL) : RA(boom_throw_side_walkR);
        case Grip::Explosive: return left ? RA(anchor_bom_side_walkL) : RA(anchor_bom_side_walkR);
        case Grip::TwoHand: case Grip::Hammer: return left ? RA(fighter_side_walkL_long) : RA(fighter_side_walkR_long);
        default: return left ? RA(anchor_side_walkL) : RA(anchor_side_walkR);
    }
}

// The spells and songs: which magic pose (fire, wind or soul) a spell is cast with.
AnimSeq CastFor(royale::ItemId ability) {
    using royale::ItemId;
    switch (ability) {
        case ItemId::DinsFire: case ItemId::BoleroOfFire: return AnimSeq(RA(magic_honoo1), false).Add(RA(magic_honoo2)).Add(RA(magic_honoo3));
        case ItemId::FaroresWind: case ItemId::NocturneOfShadow: case ItemId::EponasSong: case ItemId::MinuetOfForest:
            return AnimSeq(RA(magic_kaze1), false).Add(RA(magic_kaze2)).Add(RA(magic_kaze3));
        case ItemId::LensOfTruth: return AnimSeq(RA(normal_okarina_start), false);   // held up to the eye
        case ItemId::MagicBeans: return AnimSeq(RA(normal_put), false);               // planted at his feet
        default: return AnimSeq(RA(magic_tamashii1), false).Add(RA(magic_tamashii2)).Add(RA(magic_tamashii3));
    }
}

AnimSeq SeqFor(uint8_t anim, royale::ItemId weapon, int combo, royale::ItemId ability, Player* player) {
    using royale::Anim;
    using royale::ItemId;
    const Grip grip = GripOf(weapon);
    switch (static_cast<Anim>(anim)) {
        case Anim::Walk: return AnimSeq(RA(normal_walk), true);
        case Anim::Run: case Anim::Sprint: return AnimSeq(RA(normal_run), true);   // a sprint is the same run, played faster
        case Anim::Attack:
            if (grip == Grip::Bow || grip == Grip::Explosive || grip == Grip::Boomerang || grip == Grip::Hook || grip == Grip::Ocarina || grip == Grip::Bare)
                return AnimSeq(RA(fighter_normal_kiru), false).Add(RA(fighter_normal_kiru_end));   // bashing with whatever is in hand
            return SwingFor(grip, combo);
        case Anim::JumpSlash:
            if (grip == Grip::Hammer) return AnimSeq(RA(hammer_hit), false).Add(RA(hammer_hit_end));
            return AnimSeq(RA(fighter_Lpower_jump_kiru), false).Add(RA(fighter_Lpower_jump_kiru_hit)).Add(RA(fighter_Lpower_jump_kiru_end));
        case Anim::SpinAttack:
            if (grip == Grip::TwoHand || grip == Grip::Hammer) return AnimSeq(RA(fighter_Lrolling_kiru), false).Add(RA(fighter_Lrolling_kiru_end));
            return AnimSeq(RA(fighter_rolling_kiru), false).Add(RA(fighter_rolling_kiru_end));
        case Anim::Shoot:
            if (grip == Grip::Hook) return AnimSeq(RA(hook_shot_ready), false).Add(RA(hook_wait), true);
            return AnimSeq(RA(bow_bow_shoot), false).Add(RA(bow_bow_shoot_end)).Add(RA(bow_bow_wait), true);
        case Anim::Throw:
            if (ability == ItemId::Hookshot || ability == ItemId::Longshot) return AnimSeq(RA(hook_shot_ready), false).Add(RA(hook_wait), true);
            if (weapon == ItemId::Boomerang) return AnimSeq(RA(boom_throwR), false).Add(RA(boom_throw_wait2waitR)).Add(RA(boom_throw_waitR), true);
            return AnimSeq(RA(normal_throw), false);
        case Anim::Drink: return AnimSeq(RA(bottle_drink_demo_start), false).Add(RA(bottle_drink_demo_wait)).Add(RA(bottle_drink_demo_end));
        case Anim::Play: return AnimSeq(RA(normal_okarina_start), false).Add(RA(normal_okarina_swing), true);
        case Anim::Cast: return CastFor(ability);
        case Anim::Roll: return AnimSeq(RA(normal_landing_roll), false);
        case Anim::HopL: return AnimSeq(RA(fighter_Lside_jump), false).Add(RA(fighter_Lside_jump_end));
        case Anim::HopR: return AnimSeq(RA(fighter_Rside_jump), false).Add(RA(fighter_Rside_jump_end));
        case Anim::Backflip: return AnimSeq(RA(fighter_backturn_jump), false).Add(RA(fighter_backturn_jump_end));
        case Anim::Guard:
            if (player != nullptr && player->currentShield == PLAYER_SHIELD_NONE) return AnimSeq(RA(normal_wait), true);
            if (grip == Grip::TwoHand || grip == Grip::Hammer) return AnimSeq(RA(fighter_defense_long), false).Add(RA(fighter_defense_long_wait), true);
            return AnimSeq(RA(anchor_waitR2defense), false).Add(RA(anchor_waitR_defense_wait), true);
        case Anim::ItemGet: return AnimSeq(RA(demo_get_itemB), false);
        case Anim::OpenChest: return AnimSeq(player != nullptr && player->ageProperties != nullptr ? player->ageProperties->unk_98 : RA(demo_Tbox_open), false);
        case Anim::Jump: return AnimSeq(RA(normal_run_jump), false).Add(RA(normal_landing));
        case Anim::Swim: return AnimSeq(RA(swimer_swim), true);          // a bot swimming (the strokes the game gives a swimmer)
        case Anim::Tread: return AnimSeq(RA(swimer_swim_wait), true);    // and treading water
        case Anim::Climb: return AnimSeq(RA(normal_Fclimb_upL), true);   // hand over hand up a cliff or an ivy wall
        case Anim::Hurt: return AnimSeq(RA(normal_front_shit), false);
        case Anim::Dead: return AnimSeq(RA(normal_front_downA), false).Add(RA(normal_front_downB));
        case Anim::SideL: return AnimSeq(SideStepFor(grip, true), true);
        case Anim::SideR: return AnimSeq(SideStepFor(grip, false), true);
        case Anim::Back: return AnimSeq(grip == Grip::Bow || grip == Grip::Hook ? SideStepFor(grip, false) : RA(anchor_back_walk), true);
        case Anim::Stance: return AnimSeq(grip == Grip::OneHand && player != nullptr && player->currentShield == PLAYER_SHIELD_NONE ? RA(normal_wait) : StanceFor(grip), true);
        case Anim::Emote1: return AnimSeq(RA(demo_bikkuri), false);     // startled: "Wow!"
        case Anim::Emote2: return AnimSeq(RA(demo_jibunmiru), false);   // looks at his own hands
        case Anim::Emote3: return AnimSeq(RA(demo_kaoage), false).Add(RA(demo_kaoage_wait), true);   // looks up
        case Anim::Emote4: return AnimSeq(RA(demo_kenmiru1), false).Add(RA(demo_kenmiru1_wait), true);   // admires a sword
        case Anim::Emote5: return AnimSeq(RA(normal_wait), true);       // the chicken dance poses the limbs itself (ApplyChickenDance)
        case Anim::Emote6: return AnimSeq(RA(demo_get_itemA), false);   // holds his prize high, the item-get pose
        case Anim::Emote7: return AnimSeq(RA(demo_kousan), false);      // gives up
        case Anim::Emote8: return AnimSeq(RA(demo_kakeyori_mimawasi), false).Add(RA(demo_kakeyori_wait), true);   // looks all around
        default: return AnimSeq(RA(normal_wait), true);                 // Idle and anything newer than this build
    }
}
// The first animation of an action (the corpses' emote doubles use it).
LinkAnimationHeader* AnimFor(uint8_t anim, royale::ItemId weapon = royale::ItemId::BasicSword, int combo = 0) {
    return SeqFor(anim, weapon, combo, royale::ItemId::DinsFire, nullptr).step[0].anim;
}

// ---- how other players sound: the game's own effects and Link's voice, from where they stand -----------------------------------

constexpr float kPuppetHearing = 1600.0f;   // sounds further off than this are not played at all (the game fades them with distance anyway)
int gPuppetSfxThisFrame = 0;               // a cap, so 31 players at once don't use up every sound channel
u32 gPuppetSfxFrame = 0;

bool PuppetAudible(const Actor* actor) {
    if (gPlayState == nullptr) return false;
    if (gPuppetSfxFrame != gPlayState->gameplayFrames) { gPuppetSfxFrame = gPlayState->gameplayFrames; gPuppetSfxThisFrame = 0; }
    if (gPuppetSfxThisFrame >= 6) return false;
    const Player* me = GET_PLAYER(gPlayState);
    const float dx = actor->world.pos.x - me->actor.world.pos.x, dz = actor->world.pos.z - me->actor.world.pos.z;
    return dx * dx + dz * dz < kPuppetHearing * kPuppetHearing;
}
void PuppetSfx(Actor* actor, u16 sfx) {
    if (!PuppetAudible(actor)) return;
    gPuppetSfxThisFrame++;
    Audio_PlaySoundGeneral(sfx, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}
// Link's voice: the child and adult voices are separate sounds, as in the game.
void PuppetVoice(Player* player, u16 sfx) { PuppetSfx(&player->actor, static_cast<u16>(sfx + player->ageProperties->unk_92)); }
// Footsteps, jumps and landings sound like the ground they are on (grass, dirt, stone, water...).
u16 PuppetFloorSfx(Player* player, u16 sfx) { return static_cast<u16>(sfx + player->floorSfxOffset + player->ageProperties->unk_94); }

void PuppetStep(Player* player, float speedPerFrame) {
    if (!PuppetAudible(&player->actor)) return;
    gPuppetSfxThisFrame++;
    func_800F4010(&player->actor.projectedPos, PuppetFloorSfx(player, NA_SE_PL_WALK_GROUND), speedPerFrame);
}

// A spell or a song's own sound when somebody uses it (the tune itself is played by PlaySongMelody).
u16 AbilitySfx(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DinsFire: case ItemId::BoleroOfFire: return NA_SE_PL_MAGIC_FIRE;
        case ItemId::FaroresWind: case ItemId::NocturneOfShadow: return NA_SE_PL_MAGIC_WIND_WARP;
        case ItemId::NayrusLove: case ItemId::PreludeOfLight: return NA_SE_PL_MAGIC_SOUL_NORMAL;
        case ItemId::Hookshot: case ItemId::Longshot: return NA_SE_IT_HOOKSHOT_CHAIN;
        case ItemId::ShockwaveGrenade: return NA_SE_IT_BOMB_EXPLOSION;
        case ItemId::MagicBeans: return NA_SE_PL_PLANT_GROW_UP;
        case ItemId::LensOfTruth: return NA_SE_PL_MAGIC_SOUL_FLASH;
        default: return NA_SE_PL_MAGIC_SOUL_BALL;
    }
}
u16 AbilityVoice(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DinsFire: case ItemId::BoleroOfFire: return NA_SE_VO_LI_MAGIC_ATTACK;
        case ItemId::FaroresWind: case ItemId::NocturneOfShadow: return NA_SE_VO_LI_MAGIC_FROL;
        case ItemId::NayrusLove: case ItemId::PreludeOfLight: return NA_SE_VO_LI_MAGIC_NALE;
        default: return 0;
    }
}

// The sound of an action starting: the swish of the swing and Link's shout, the bow string, the bottle, the roll...
void ActionSounds(Player* player, uint8_t anim, royale::ItemId weapon, int combo) {
    using royale::Anim;
    using royale::ItemId;
    const Grip grip = GripOf(weapon);
    switch (static_cast<Anim>(anim)) {
        case Anim::Attack:
            if (grip == Grip::Hammer) PuppetSfx(&player->actor, NA_SE_IT_HAMMER_SWING);
            else PuppetSfx(&player->actor, weapon == ItemId::MasterSword || weapon == ItemId::GildedSword ? NA_SE_IT_MASTER_SWORD_SWING : grip == Grip::TwoHand ? NA_SE_IT_SWORD_SWING_HARD : NA_SE_IT_SWORD_SWING);
            PuppetVoice(player, combo % 4 == 2 || grip == Grip::Hammer || grip == Grip::TwoHand ? NA_SE_VO_LI_SWORD_L : NA_SE_VO_LI_SWORD_N);
            break;
        case Anim::JumpSlash:
            PuppetVoice(player, NA_SE_VO_LI_SWORD_L);
            PuppetSfx(&player->actor, PuppetFloorSfx(player, NA_SE_PL_JUMP));
            break;
        case Anim::SpinAttack:
            PuppetSfx(&player->actor, NA_SE_IT_ROLLING_CUT);
            PuppetVoice(player, NA_SE_VO_LI_SWORD_L);
            break;
        case Anim::Shoot:
            PuppetSfx(&player->actor, grip == Grip::Hook ? NA_SE_IT_HOOKSHOT_READY : weapon == ItemId::Slingshot || weapon == ItemId::TripleSlingshot ? NA_SE_IT_SLING_DRAW : NA_SE_IT_BOW_DRAW);
            break;
        case Anim::Throw: PuppetVoice(player, NA_SE_VO_LI_SWORD_N); PuppetSfx(&player->actor, NA_SE_PL_THROW); break;
        case Anim::Drink: PuppetSfx(&player->actor, NA_SE_PL_PUT_OUT_ITEM); break;
        case Anim::Roll: PuppetSfx(&player->actor, NA_SE_PL_ROLL); break;
        case Anim::HopL: case Anim::HopR: case Anim::Backflip: case Anim::Jump:
            PuppetVoice(player, NA_SE_VO_LI_AUTO_JUMP);
            PuppetSfx(&player->actor, PuppetFloorSfx(player, NA_SE_PL_JUMP));
            break;
        case Anim::Guard: PuppetSfx(&player->actor, NA_SE_IT_SHIELD_POSTURE); break;
        case Anim::OpenChest: PuppetSfx(&player->actor, NA_SE_EV_TBOX_OPEN); break;
        case Anim::ItemGet: PuppetSfx(&player->actor, NA_SE_SY_GET_ITEM); break;
        case Anim::Emote1: PuppetVoice(player, NA_SE_VO_LI_SURPRISE); break;
        case Anim::Hurt: PuppetVoice(player, NA_SE_VO_LI_DAMAGE_S); break;
        case Anim::Dead: PuppetVoice(player, NA_SE_VO_LI_DOWN); break;
        default: break;
    }
}

// Songs: when somebody near you plays one, you hear the real tune on the ocarina, one at a time.
int OcarinaSongOf(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::ZeldasLullaby: return OCARINA_SONG_LULLABY;
        case ItemId::EponasSong: return OCARINA_SONG_EPONAS;
        case ItemId::SariasSong: return OCARINA_SONG_SARIAS;
        case ItemId::SunsSong: return OCARINA_SONG_SUNS;
        case ItemId::SongOfTime: return OCARINA_SONG_TIME;
        case ItemId::SongOfStorms: return OCARINA_SONG_STORMS;
        case ItemId::MinuetOfForest: return OCARINA_SONG_MINUET;
        case ItemId::BoleroOfFire: return OCARINA_SONG_BOLERO;
        case ItemId::SerenadeOfWater: return OCARINA_SONG_SERENADE;
        case ItemId::NocturneOfShadow: return OCARINA_SONG_NOCTURNE;
        case ItemId::RequiemOfSpirit: return OCARINA_SONG_REQUIEM;
        case ItemId::PreludeOfLight: return OCARINA_SONG_PRELUDE;
        case ItemId::FairyOcarina: { static const int simple[] = { OCARINA_SONG_LULLABY, OCARINA_SONG_EPONAS, OCARINA_SONG_SARIAS, OCARINA_SONG_SUNS, OCARINA_SONG_TIME, OCARINA_SONG_STORMS }; return simple[static_cast<int>(Rand_ZeroOne() * 5.99f)]; }
        case ItemId::OcarinaOfTime: return static_cast<int>(Rand_ZeroOne() * 11.99f);   // any of the twelve
        default: return -1;
    }
}
struct SongPlayback { bool on = false; double started = 0; };
SongPlayback gSongPlayback;
bool SongBlocked() {
    if (gPlayState == nullptr || gPlayState->pauseCtx.state != 0) return true;
    return (GET_PLAYER(gPlayState)->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) != 0;   // you are playing the real ocarina yourself
}
void PlaySongMelody(int song) {
    if (song < 0 || gSongPlayback.on || SongBlocked()) return;
    Audio_OcaSetInstrument(1);
    Audio_OcaSetSongPlayback(static_cast<s8>(song + 1), 1);
    gSongPlayback = { true, ImGui::GetTime() };
}
void UpdateSongMelody() {
    if (!gSongPlayback.on) return;
    const double age = ImGui::GetTime() - gSongPlayback.started;
    const OcarinaStaff* staff = Audio_OcaGetDisplayingStaff();
    if ((age > 0.5 && staff != nullptr && staff->state == 0) || age > 8.0 || (gPlayState != nullptr && gPlayState->pauseCtx.state != 0)) {
        Audio_OcaSetSongPlayback(0, 0);
        Audio_OcaSetInstrument(0);
        gSongPlayback.on = false;
    }
}

// ---- the game's own spell and song effects, on whoever cast them ---------------------------------------------------------------

// Din's Fire, Farore's Wind, Nayru's Love and the ocarina's song swirls are the game's own actors. They are written for Link alone: every frame
// they find him at the head of the player list and follow him. To put one on another player, that player stands in at the head of the list
// while the effect sets itself up, updates and draws, so it follows them instead. Anything an effect would do to *your* game is undone for
// other players' casts: the clean-up that resets the magic meter (the server's), Din's Fire's red tint over the screen and its burning of
// whatever is near, and the Nayru's Love timer in the save (each diamond keeps its own, so it lasts as long as the server's protection).
enum class SpellKind : uint8_t { Fire, Wind, Love, Song };
struct SpellFx {
    Actor* actor = nullptr;
    uint16_t who = 0;       // the caster's player id (unused for your own)
    bool self = false;
    SpellKind kind = SpellKind::Fire;
    ActorFunc update = nullptr, draw = nullptr, destroy = nullptr;   // the effect's own
    int framesLeft = 0;     // Nayru's Love: game frames the diamond still stands
    int steps = 1;          // updates per frame: the song swirls run at double speed, so they don't hide a fight for five seconds
};
std::vector<SpellFx> gSpells;
constexpr int kLoveFrames = 4 * 20;   // Nayru's Love protects for 4 seconds (shared/items.h)
float gSelfInvulnLeft = 0.0f;         // seconds the server still protects you: your own diamond stands that long

SpellFx* SpellOf(const Actor* a) {
    for (SpellFx& f : gSpells) if (f.actor == a) return &f;
    return nullptr;
}
Actor* CasterOf(PlayState* play, const SpellFx& f) {
    if (f.self) return &GET_PLAYER(play)->actor;
    auto it = gActorOf.find(f.who);
    return it != gActorOf.end() ? it->second : nullptr;
}
// The caster at the head of the player list for as long as this lives.
struct CasterFirst {
    ActorListEntry* list;
    Actor* old;
    CasterFirst(PlayState* play, Actor* caster) : list(&play->actorCtx.actorLists[ACTORCAT_PLAYER]), old(list->head) {
        if (caster != nullptr) list->head = caster;
    }
    ~CasterFirst() { list->head = old; }
};
// What the diamond reads as the Nayru's Love timer: steady while it lasts, then the game's own flicker and fade over the last second, after
// which it ends itself (and gives the caster back the usual invincibility timer).
s16 LoveTimer(int framesLeft) { return static_cast<s16>(framesLeft > 20 ? 600 : 1200 - std::max(0, framesLeft)); }

void Spell_Draw(Actor* a, PlayState* play);
void Spell_Update(Actor* a, PlayState* play) {
    SpellFx* f = SpellOf(a);
    Actor* caster = f != nullptr ? CasterOf(play, *f) : nullptr;
    if (f == nullptr || caster == nullptr || f->update == nullptr || f->framesLeft < -40) { Actor_Kill(a); return; }
    if (f->kind == SpellKind::Love && f->self && gSelfInvulnLeft > 1.0f) f->framesLeft = std::max(f->framesLeft, static_cast<int>(gSelfInvulnLeft * 20.0f));
    const s16 love = gSaveContext.nayrusLoveTimer;
    {
        CasterFirst first(play, caster);
        for (int i = 0; i < f->steps; i++) {
            if (f->kind == SpellKind::Love) gSaveContext.nayrusLoveTimer = LoveTimer(f->framesLeft);
            f->update(a, play);
            if (a->update == nullptr) break;   // it ended itself
            // The effects change their own update and draw as they go (gathering, then expanding...): keep ours in front.
            if (a->update != Spell_Update) { f->update = a->update; a->update = Spell_Update; }
            if (a->draw != nullptr && a->draw != Spell_Draw) { f->draw = a->draw; a->draw = Spell_Draw; }
        }
    }
    gSaveContext.nayrusLoveTimer = love;
    if (f->kind == SpellKind::Love) f->framesLeft--;
    if (!f->self && f->kind == SpellKind::Fire) reinterpret_cast<MagicFire*>(a)->collider.base.atFlags &= ~AT_ON;   // the server decides who burns
}
void Spell_Draw(Actor* a, PlayState* play) {
    SpellFx* f = SpellOf(a);
    Actor* caster = f != nullptr ? CasterOf(play, *f) : nullptr;
    if (f == nullptr || caster == nullptr || f->draw == nullptr) return;
    if (!f->self && f->kind == SpellKind::Fire) reinterpret_cast<MagicFire*>(a)->screenTintIntensity = 0.0f;   // only the caster's screen glows
    const s16 love = gSaveContext.nayrusLoveTimer;
    if (f->kind == SpellKind::Love) gSaveContext.nayrusLoveTimer = LoveTimer(f->framesLeft);
    {
        CasterFirst first(play, caster);
        f->draw(a, play);
    }
    gSaveContext.nayrusLoveTimer = love;
}
void Spell_Destroy(Actor* a, PlayState* play) {
    SpellFx* f = SpellOf(a);
    if (f == nullptr) return;
    const auto magicState = gSaveContext.magicState, prevMagicState = gSaveContext.prevMagicState;
    const s16 love = gSaveContext.nayrusLoveTimer;
    if (f->destroy != nullptr) {
        CasterFirst first(play, CasterOf(play, *f));
        f->destroy(a, play);
    }
    gSaveContext.magicState = magicState;   // the effects reset the magic meter when they end; the meter is the server's
    gSaveContext.prevMagicState = prevMagicState;
    gSaveContext.nayrusLoveTimer = love;
    gSpells.erase(std::remove_if(gSpells.begin(), gSpells.end(), [a](const SpellFx& s) { return s.actor == a; }), gSpells.end());
}

// Spawns one of the game's spell or song effects on a caster (your own Link when `self`).
void SpawnSpell(PlayState* play, s16 actorId, s16 params, SpellKind kind, Actor* caster, uint16_t who, bool self) {
    if (play == nullptr || caster == nullptr || gSpells.size() >= 12) return;
    SpellFx f;
    f.who = who;
    f.self = self;
    f.kind = kind;
    f.framesLeft = kLoveFrames;
    f.steps = kind == SpellKind::Song ? 2 : 1;
    const s16 love = gSaveContext.nayrusLoveTimer;
    if (kind == SpellKind::Love) gSaveContext.nayrusLoveTimer = LoveTimer(f.framesLeft);   // straight to the diamond: the casting orb dims the whole scene
    Actor* a = nullptr;
    {
        CasterFirst first(play, caster);
        a = Actor_Spawn(&play->actorCtx, play, actorId, caster->world.pos.x, caster->world.pos.y, caster->world.pos.z, 0, 0, 0, params, true);
    }
    gSaveContext.nayrusLoveTimer = love;
    if (a == nullptr) return;
    f.actor = a;
    f.update = a->update;
    f.draw = a->draw;
    f.destroy = a->destroy;
    a->update = Spell_Update;
    if (a->draw != nullptr) a->draw = Spell_Draw;
    a->destroy = Spell_Destroy;
    gSpells.push_back(f);
}

// The swirl the game shows when Link plays a song on the ocarina, for the songs that have one (the same table the game's message code uses).
bool SongSwirl(royale::ItemId song, s16* actorId, s16* params) {
    using royale::ItemId;
    *params = 0;
    switch (song) {
        case ItemId::SariasSong: *actorId = ACTOR_OCEFF_WIPE3; return true;
        case ItemId::EponasSong: *actorId = ACTOR_OCEFF_WIPE2; return true;
        case ItemId::ZeldasLullaby: *actorId = ACTOR_OCEFF_WIPE; return true;
        case ItemId::SongOfTime: *actorId = ACTOR_OCEFF_WIPE; *params = 1; return true;
        // The warp songs, the Sun's Song and the Song of Storms do more than show a swirl in the game (a warp, a change of time, rain), so they
        // show the plain one the game uses for the scarecrow's song.
        case ItemId::MinuetOfForest: case ItemId::BoleroOfFire: case ItemId::SerenadeOfWater: case ItemId::RequiemOfSpirit:
        case ItemId::NocturneOfShadow: case ItemId::PreludeOfLight: case ItemId::SunsSong: case ItemId::SongOfStorms:
            *actorId = ACTOR_OCEFF_WIPE4; return true;
        default: return false;
    }
}

// What a spell, a song or a gadget looks like when somebody uses it. The spells are the game's own Din's Fire, Nayru's Love and Farore's Wind
// on whoever cast them, and a song you play yourself brings up the game's own swirl. The rest (and other players' songs, whose swirl fills
// the screen of whoever played them) are made from the game's own particle effects: its glitter, shock rings and explosions. `at` is the
// user's feet; `caster` is their actor.
void PowerFx(PlayState* play, royale::ItemId item, const Vec3f& at, bool self, Actor* caster, uint16_t who) {
    using royale::ItemId;
    auto ring = [&](Color_RGBA8 prim, Color_RGBA8 env, float radius, int count, float rise, s16 scale) {
        for (int i = 0; i < count; i++) {
            const float a = i * (6.2831853f / count);
            Vec3f pos = { at.x + std::sin(a) * radius, at.y + 6.0f + Rand_ZeroOne() * 12.0f, at.z + std::cos(a) * radius };
            Vec3f vel = { std::sin(a) * 0.6f, rise * (0.6f + Rand_ZeroOne()), std::cos(a) * 0.6f }, accel = { 0, 0.05f, 0 };
            EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, scale, 30);
        }
    };
    auto shock = [&](float y) { Vec3f pos = { at.x, at.y + y, at.z }, vel = { 0, 0, 0 }, accel = { 0, 0, 0 }; EffectSsBlast_SpawnWhiteShockwave(play, &pos, &vel, &accel); };
    auto spell = [&](s16 actorId, SpellKind kind) { SpawnSpell(play, actorId, 0, kind, caster, who, self); };
    if (self && (royale::IsSong(item) || item == ItemId::FairyOcarina || item == ItemId::OcarinaOfTime)) {
        s16 id = 0, params = 0;
        if (SongSwirl(item, &id, &params)) SpawnSpell(play, id, params, SpellKind::Song, caster, who, true);
    }
    switch (item) {
        case ItemId::DinsFire: spell(ACTOR_MAGIC_FIRE, SpellKind::Fire); break;
        case ItemId::NayrusLove: spell(ACTOR_MAGIC_DARK, SpellKind::Love); break;
        case ItemId::FaroresWind: spell(ACTOR_MAGIC_WIND, SpellKind::Wind); break;
        case ItemId::BoleroOfFire:
            ring({ 255, 200, 0, 255 }, { 255, 0, 0, 255 }, 60.0f, 20, 2.2f, 110); ring({ 255, 90, 0, 255 }, { 200, 0, 0, 255 }, 110.0f, 28, 1.4f, 90); shock(30.0f);
            break;
        case ItemId::MinuetOfForest: ring({ 200, 255, 120, 255 }, { 0, 200, 0, 255 }, 55.0f, 24, 2.6f, 90); break;
        case ItemId::SerenadeOfWater: ring({ 170, 230, 255, 255 }, { 0, 150, 255, 255 }, 55.0f, 24, 2.6f, 90); shock(20.0f); break;
        case ItemId::NocturneOfShadow: ring({ 230, 160, 255, 255 }, { 200, 50, 255, 255 }, 55.0f, 24, 2.6f, 90); break;
        case ItemId::RequiemOfSpirit: ring({ 255, 210, 120, 255 }, { 255, 150, 0, 255 }, 70.0f, 24, 2.0f, 100); shock(30.0f); break;
        case ItemId::PreludeOfLight: case ItemId::ZeldasLullaby: ring({ 255, 255, 220, 255 }, { 255, 230, 100, 255 }, 40.0f, 18, 2.8f, 100); break;
        case ItemId::SunsSong: ring({ 255, 255, 180, 255 }, { 255, 220, 0, 255 }, 80.0f, 28, 2.0f, 110); shock(40.0f); break;
        case ItemId::SongOfTime: ring({ 190, 255, 255, 255 }, { 80, 190, 240, 255 }, 80.0f, 28, 2.0f, 110); shock(40.0f); break;
        case ItemId::EponasSong: ring({ 255, 200, 140, 255 }, { 200, 120, 40, 255 }, 45.0f, 16, 1.6f, 80); break;
        case ItemId::SariasSong: ring({ 190, 255, 190, 255 }, { 0, 220, 100, 255 }, 45.0f, 16, 1.6f, 80); break;
        case ItemId::SongOfStorms:
            if (self) Environment_AddLightningBolts(play, 2);
            ring({ 255, 255, 255, 255 }, { 120, 120, 255, 255 }, 70.0f, 20, 2.4f, 90);
            break;
        case ItemId::ShockwaveGrenade:   // a purple blast at the feet that throws the user up
            ring({ 235, 190, 255, 255 }, { 160, 60, 255, 255 }, 30.0f, 16, 3.4f, 110);
            ring({ 210, 150, 255, 255 }, { 120, 40, 230, 255 }, 65.0f, 22, 1.2f, 90);
            shock(4.0f);
            break;
        case ItemId::MagicBeans: ring({ 180, 255, 140, 255 }, { 60, 200, 0, 255 }, 25.0f, 12, 2.0f, 80); break;
        case ItemId::LensOfTruth: ring({ 235, 190, 255, 255 }, { 150, 80, 220, 255 }, 25.0f, 12, 1.5f, 80); break;
        default: break;
    }
}

// Someone used an ability: the spell's sound and Link's shout from where they stand, and the tune if it was a song and they are close.
void AbilityFx(uint16_t who, royale::ItemId item, bool self, float x, float z) {
    gLastAbility[who] = item;
    gLastAbilityAt[who] = ImGui::GetTime();
    if (!InGame()) return;
    const bool song = royale::IsSong(item) || item == royale::ItemId::FairyOcarina || item == royale::ItemId::OcarinaOfTime;
    Player* me = GET_PLAYER(gPlayState);
    const float d = std::hypot(x - me->actor.world.pos.x, z - me->actor.world.pos.z);
    if (song && (self || d < 900.0f)) PlaySongMelody(OcarinaSongOf(item));
    if (self) return;   // your own spell already made its sound and flash when you used it
    auto a = gActorOf.find(who);
    if (a == gActorOf.end() || a->second == nullptr) return;
    PowerFx(gPlayState, item, a->second->world.pos, false, a->second, who);
    Player* p = (Player*)a->second;
    if (!song) PuppetSfx(&p->actor, AbilitySfx(item));
    if (const u16 v = AbilityVoice(item)) PuppetVoice(p, v);
}

// ---- the puppet itself ------------------------------------------------------------------------------------------------------------

// Each puppet's place in its current action, how fast it is really moving (to match its legs to it), and when it last made a sound.
struct PuppetMotion {
    uint8_t anim = 255;         // the action being shown
    AnimSeq seq;
    int step = 0;
    int combo = 0;
    royale::ItemId seqAbility = royale::ItemId::Count;
    float lastX = 0, lastZ = 0;
    bool havePos = false;
    float speed = 0;            // units per game frame, smoothed
    float prevFrame = 0;        // for footsteps
    int idleFrames = 0;         // standing about: now and then a look round
    float jumpT = -1;           // a bot's jump arc, 0..1 (the server only says which floor it is on, so the jump is drawn here)
    int floorCheck = 0;
    royale::ItemId heldWeapon = royale::ItemId::Count;
    int flinch = 0;
    // in a cart (PuppetRide): climbing on (0) or sat down (1), the gait shown, and where the saddle was (for climbing down)
    uint8_t rideStage = 0, rideGait = 0;
    royale::Seat rideSeat = royale::Seat::Driver;
    Vec3f rideAt = { 0, 0, 0 };
    s16 rideYaw = 0;
    int dismount = 0;
};
std::unordered_map<const Actor*, PuppetMotion> gMotion;
// Carts (the section "carts: the Lon Lon Buggy" below).
bool PuppetRide(PlayState* play, Player* player, PuppetMotion& m, uint16_t id);
bool CartInput(Input& in, const royale::HudState& hud);
std::string CartPromptText();
void DrawCartHud(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);
void DrawCartsOnMap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap, const std::function<bool(ImVec2)>& inside);
bool CartTargetAt(uint16_t id, float* x, float* z);

void StartStep(PlayState* play, Player* player, PuppetMotion& m, int step, float morph) {
    m.step = step;
    const AnimStep& st = m.seq.step[step];
    LinkAnimation_Change(play, &player->skelAnime, st.anim, 1.0f, 0.0f, Animation_GetLastFrame(st.anim), st.loop ? ANIMMODE_LOOP : ANIMMODE_ONCE, morph);
    m.prevFrame = 0;
}
void StartSeq(PlayState* play, Player* player, PuppetMotion& m, const AnimSeq& seq, float morph) {
    m.seq = seq;
    if (m.seq.count == 0) m.seq = AnimSeq(RA(normal_wait), true);
    StartStep(play, player, m, 0, morph);
}

// Which ground the puppet stands on, for its footsteps (checked a few times a second).
void UpdatePuppetFloor(PlayState* play, Player* player, PuppetMotion& m) {
    if (m.floorCheck-- > 0) return;
    m.floorCheck = 8;
    CollisionPoly poly;
    s32 bgId = BGCHECK_SCENE;
    Vec3f pos = { player->actor.world.pos.x, player->actor.world.pos.y + 40.0f, player->actor.world.pos.z };
    const float y = BgCheck_AnyRaycastFloor2(&play->colCtx, &poly, &bgId, &pos);
    player->floorSfxOffset = y > BGCHECK_Y_MIN + 1.0f ? SurfaceType_GetSfx(&play->colCtx, &poly, bgId) : 0;
}


void Puppet_Init(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;
    // Modeled on upstream Shipwright's Anchor DummyPlayer_Init (itself modeled on EnTorch2_Init and Player_Init).
    actor->room = -1;
    player->itemAction = player->heldItemAction = -1;
    player->heldItemId = ITEM_NONE;
    Player_UseItem(play, player, ITEM_NONE);
    Player_SetModelGroup(player, Player_ActionToModelGroup(player, player->heldItemAction));
    play->playerInit(player, play, gPlayerSkelHeaders[gSaveContext.linkAge]);

    // A puppet never swings a weapon, so don't let it hold one of the limited weapon-trail effect slots.
    Effect_Delete(play, player->meleeWeaponEffectIndex);
    player->meleeWeaponEffectIndex = TOTAL_EFFECT_COUNT;

    play->func_11D54(player, play);

    // Other players can be Z-targeted like an enemy, which is how duels work.
    actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE;

}

// A small glow in the weapon's rarity colour around the hand that holds it: glints at the hand plus a faint halo, more of them the rarer it is,
// so you can size up what another player carries at a glance. Switch it off (or on for your own weapon) in the Royale menu.
bool HeldGlowOn(bool self);
void HeldGlow(PlayState* play, Player* player, royale::Rarity rarity, bool self) {
    if (!HeldGlowOn(self)) return;
    static const int kEvery[] = { 6, 5, 4, 3, 2 };
    if (play->gameplayFrames % kEvery[static_cast<int>(rarity)] != 0) return;
    const Vec3f& hand = player->bodyPartsPos[PLAYER_BODYPART_R_HAND];
    Vec3f pos = hand;
    pos.x += (Rand_ZeroOne() - 0.5f) * 14.0f;
    pos.y += (Rand_ZeroOne() - 0.5f) * 14.0f + 6.0f;
    pos.z += (Rand_ZeroOne() - 0.5f) * 14.0f;
    Vec3f vel = { 0.0f, 0.3f, 0.0f };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    Color_RGBA8 prim = RarityColor(rarity);
    Color_RGBA8 env = prim;
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 40, 10);
}

bool HangingFromGlider(const royale::PuppetState* st, const Actor* actor, PlayState* play) {
    return st != nullptr && st->alive && (gStateNow == royale::MatchState::Countdown || gStateNow == royale::MatchState::Drop) &&
           actor->world.pos.y - GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y - 1000.0f) > 120.0f;
}

// What is in someone's hand right now, as the game shows it: their weapon, except while they use something else the way Link does - the
// bottle while they drink, the ocarina while they play a song, the hookshot while they fire one. A thrown boomerang has left the hand.
Look ActionLook(uint8_t anim, royale::ItemId weapon, royale::ItemId ability, double abilityAge, royale::ItemId bottle) {
    using royale::Anim;
    using royale::ItemId;
    switch (static_cast<Anim>(anim)) {
        case Anim::Drink: return BottleLook(bottle);
        case Anim::Play:
            if (weapon == ItemId::FairyOcarina || ability == ItemId::FairyOcarina) return LookFor(ItemId::FairyOcarina);
            return LookFor(ItemId::OcarinaOfTime);
        case Anim::Throw:
            if ((ability == ItemId::Hookshot || ability == ItemId::Longshot) && abilityAge < 1.2) return LookFor(ability);
            if (weapon == ItemId::Boomerang) return LookFor(ItemId::Count);
            break;
        default: break;
    }
    return LookFor(weapon);
}
Look PuppetLook(const royale::PuppetState& s) {
    const auto ab = gLastAbility.find(s.id);
    const auto at = gLastAbilityAt.find(s.id);
    const royale::ItemId ability = ab != gLastAbility.end() ? ab->second : royale::ItemId::Count;
    const double age = at != gLastAbilityAt.end() ? ImGui::GetTime() - at->second : 1e9;
    return ActionLook(s.anim, s.weapon, ability, age, royale::ItemId::RedPotion);   // which potion others drink isn't sent: the red one
}

void Puppet_Update(Actor* actor, PlayState* play) {
    Feat("other players: update");
    Player* player = (Player*)actor;
    auto idIt = gPuppetOf.find(actor);
    auto stIt = idIt == gPuppetOf.end() ? gState.end() : gState.find(idIt->second);
    if (stIt == gState.end()) {
        Actor_Kill(actor);
        return;
    }
    const royale::PuppetState& s = stIt->second;

    actor->world.pos.x = s.x;
    actor->world.pos.z = s.z;
    // Bots are simulated in x and z with a height above the floor (BotY). Humans report their own height.
    actor->world.pos.y = s.isBot ? BotY(play, s.x, s.z, s.y) : s.y;
    actor->shape.rot.y = s.rot;
    actor->world.rot.y = s.rot;
    {   // the recoil of a blow, tilted about the feet on top of the animation (a rider's seat sets its own tilt later)
        auto rc = gHitRecoil.find(idIt->second);
        float pitch = 0, roll = 0;
        if (rc != gHitRecoil.end()) {
            rc->second.age += 1.0f / royale::kTickHz;
            RecoilTilt(rc->second, s.rot * (3.14159265f / 32768.0f), &pitch, &roll);
            if (rc->second.age > 0.5f) gHitRecoil.erase(rc);
        }
        actor->shape.rot.x = static_cast<s16>(pitch * (32768.0f / 3.14159265f));
        actor->shape.rot.z = static_cast<s16>(roll * (32768.0f / 3.14159265f));
    }
    actor->shape.shadowAlpha = 255;
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 50.0f;

    player->currentTunic = PLAYER_TUNIC_KOKIRI; // the colour comes from the player's skin, not from whatever the local save wears

    {   // Adult Power: drawn bigger (and back to size afterwards), easing between the two
        const float target = 0.01f * (s.adult ? royale::kAdultScale : 1.0f);
        const float k = actor->scale.x + (target - actor->scale.x) * 0.15f;
        actor->scale.x = actor->scale.y = actor->scale.z = k;
    }

    // Hold what the server says this player holds, and wear their shield, boots and mask.
    player->currentBoots = PlayerBootsFor(s.boots);
    player->currentMask = PlayerMaskFor(s.mask);
    const s8 shield = PlayerShieldFor(s.shield);
    Look look = PuppetLook(s);
    if (player->modelGroup != look.modelGroup || player->heldItemAction != look.itemAction || player->currentShield != shield) {
        u8 original = gSaveContext.equips.buttonItems[0];
        gSaveContext.equips.buttonItems[0] = look.buttonItem;
        player->currentShield = shield;
        player->itemAction = player->heldItemAction = look.itemAction;
        Player_SetModelGroup(player, look.modelGroup);
        gSaveContext.equips.buttonItems[0] = original;
    }
    ShowStickLength(player);   // puppets never go through the game's take-out, so the stick has no length until it is given one

    if (s.alive) HeldGlow(play, player, s.weaponRarity, false);

    // The nameplate shows the player's name, hearts left and weapon (colour coded by rarity), so you can size up everyone you see.
    {
        const int hearts = static_cast<int>(std::ceil(std::max(0.0f, s.health)));
        const std::string plate = s.name + "  " + std::to_string(hearts) + " hearts  " + ItemLabel(s.weapon, s.weaponRarity);
        std::string& shown = gPlate[actor];
        if (shown != plate) {
            NameTag_RemoveAllForActor(actor);
            NameTag_RegisterForActorWithOptions(actor, plate.c_str(), NameTagOptions{ "royale-puppet", 0, RarityColor(s.weaponRarity) });
            shown = plate;
        }
    }

    PuppetMotion& m = gMotion[actor];
    {   // how fast they really move (snapshots arrive in steps, so smoothed): the legs are matched to it
        if (m.havePos) {
            const float moved = std::hypot(s.x - m.lastX, s.z - m.lastZ);
            if (moved < 80.0f) m.speed = m.speed * 0.7f + moved * 0.3f;
        }
        m.lastX = s.x; m.lastZ = s.z; m.havePos = true;
    }
    UpdatePuppetFloor(play, player, m);
    if (m.heldWeapon != s.weapon) {   // a weapon swapped in: the game's draw sound
        if (m.heldWeapon != royale::ItemId::Count && s.alive) PuppetSfx(actor, NA_SE_PL_CHANGE_ARMS);
        m.heldWeapon = s.weapon;
    }

    {   // somebody has just loosed an arrow or thrown something: show it in flight
        static std::unordered_map<uint16_t, uint8_t> previous;
        uint8_t& before = previous[s.id];
        if (s.anim != before && s.alive && (s.anim == static_cast<uint8_t>(royale::Anim::Shoot) || s.anim == static_cast<uint8_t>(royale::Anim::Throw)))
            SpawnProjectileFrom(s.weapon, s.x, actor->world.pos.y + 45.0f, s.z, s.rot);
        before = s.anim;
    }
    if (s.alive && PuppetRide(play, player, m, s.id)) return;   // sat in a cart (or climbing down from one)
    if (HangingFromGlider(&s, actor, play)) {   // both hands up on the glider's bar (the game's ledge-hang pose)
        if (m.anim != 254) { StartSeq(play, player, m, AnimSeq(RA(normal_jump_climb_wait), true), -6.0f); m.anim = 254; }
        LinkAnimation_Update(play, &player->skelAnime);
        Vec3f ignored;
        SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
        return;
    }

    // What to show. Mostly the player's own state; but when they are seen landing a blow (or taking one) and their state doesn't say so
    // yet, the swing (or the flinch) is shown anyway, so a fight always reads as one.
    using royale::Anim;
    uint8_t want = s.anim;
    bool forced = false;
    {
        auto sw = gSwingFrames.find(s.id);
        auto fl = gFlinchFrames.find(s.id);
        const bool busy = royale::IsStrike(s.anim) || royale::IsDodge(s.anim) || OneShotAnim(s.anim);
        if (sw != gSwingFrames.end() && sw->second > 0) {
            if (!busy) {   // the blow was struck with what they hold: a shot or a throw for a slingshot, bow or bomb, a swing only for a blade or hammer
                const royale::WeaponStats ws = royale::WeaponOf(s.weapon);
                const royale::AmmoKind ak = royale::AmmoUsedBy(s.weapon);
                want = static_cast<uint8_t>(!ws.ranged ? Anim::Attack : (ak == royale::AmmoKind::Arrows || ak == royale::AmmoKind::Seeds) ? Anim::Shoot : Anim::Throw);
                forced = sw->second == 10;
            }
            sw->second--;
        } else if (fl != gFlinchFrames.end() && fl->second > 0) {
            if (!busy && s.alive) { want = static_cast<uint8_t>(Anim::Hurt); forced = fl->second == 8; }
            fl->second--;
        }
    }
    const auto ab = gLastAbility.find(s.id);
    royale::ItemId ability = ab != gLastAbility.end() ? ab->second : royale::ItemId::NayrusLove;
    {   // a throw is the hookshot only if they have just used one; otherwise it is whatever is in their hand
        auto at = gLastAbilityAt.find(s.id);
        if (want == static_cast<uint8_t>(royale::Anim::Throw) && (at == gLastAbilityAt.end() || ImGui::GetTime() - at->second > 1.2)) ability = royale::ItemId::Count;
    }
    // A spell's pose depends on which spell: if word of it arrives just after the pose began, start again with the right one.
    const bool respell = (want == static_cast<uint8_t>(Anim::Cast) || want == static_cast<uint8_t>(Anim::Throw)) && m.anim == want && m.seqAbility != ability && m.step == 0 &&
                         player->skelAnime.curFrame < 6.0f;
    // Standing about for a while: a look round, a stretch, as Link does when you leave the stick alone.
    bool fidget = false;
    if (want == static_cast<uint8_t>(Anim::Idle)) {
        if (++m.idleFrames > 110 + static_cast<int>(s.id % 7) * 20 && s.health > 1.0f) { fidget = true; m.idleFrames = 0; }
    } else {
        m.idleFrames = 0;
    }
    if (want != m.anim || forced || respell || fidget) {
        const bool strike = royale::IsStrike(want);
        if (strike && !respell) m.combo++;
        AnimSeq seq = SeqFor(want, s.weapon, m.combo, ability, player);
        if (fidget) {   // the game's own idle fidgets: a look round, a stretch, a practice swing, tugging the tunic, tapping a foot, the shield
            const Grip grip = GripOf(s.weapon);
            LinkAnimationHeader* const looks[6] = { RA(normal_wait_typeA_20f), RA(wait_typeD_20f), grip == Grip::TwoHand || grip == Grip::Hammer ? RA(wait_itemD2_20f) : RA(wait_itemD1_20f),
                                                    RA(wait_itemA_20f), RA(wait_itemB_20f), RA(wait_itemC_20f) };
            seq = AnimSeq(looks[(play->gameplayFrames / 7 + s.id) % 6], false).Add(RA(normal_wait), true);
        }
        if (want == static_cast<uint8_t>(Anim::Idle) && s.alive && s.health <= 1.0f) seq = AnimSeq(RA(wait_heat1_20f), false).Add(RA(wait_heat2_20f), true);   // nearly dead: doubled over, panting
        // Moving between walking, running and standing blends over a few frames; an action snaps in quickly, as the game does.
        const float morph = OneShotAnim(want) ? -3.0f : -6.0f;
        StartSeq(play, player, m, seq, morph);
        m.seqAbility = ability;
        if (!respell && !fidget && s.alive) ActionSounds(player, want, s.weapon, m.combo);
        m.jumpT = -1.0f;
        if (s.isBot && (want == static_cast<uint8_t>(Anim::Jump) || want == static_cast<uint8_t>(Anim::JumpSlash) || (royale::IsDodge(want) && want != static_cast<uint8_t>(Anim::Roll)))) m.jumpT = 0.0f;
        m.anim = want;
    }

    // Walking and running at the speed they really go, so the feet don't slide.
    const bool sprinting = want == static_cast<uint8_t>(Anim::Sprint);
    const bool walking = want == static_cast<uint8_t>(Anim::Walk) || want == static_cast<uint8_t>(Anim::Run) || want == static_cast<uint8_t>(Anim::SideL) ||
                         want == static_cast<uint8_t>(Anim::SideR) || want == static_cast<uint8_t>(Anim::Back) || sprinting;
    if (sprinting) {   // legs pump faster and dust kicks up, like the local player's sprint
        player->skelAnime.playSpeed = std::max(kSprintAnimSpeed, std::min(2.0f, m.speed / 5.5f));
        if (actor->bgCheckFlags & 1) SprintDust(play, actor, play->gameplayFrames);
    } else if (walking) {
        const float stride = want == static_cast<uint8_t>(Anim::Run) ? 5.5f : want == static_cast<uint8_t>(Anim::Walk) ? 2.4f : 3.0f;   // units per frame at normal speed
        player->skelAnime.playSpeed = std::clamp(m.speed / stride, 0.55f, 1.8f);
    }
    const bool finished = LinkAnimation_Update(play, &player->skelAnime);
    if (finished && !m.seq.step[m.step].loop && m.step + 1 < m.seq.count) {
        StartStep(play, player, m, m.step + 1, 0.0f);
        // Feet back on the ground after a hop, a flip or a jump; the jump slash coming down.
        if (s.alive && (royale::IsDodge(m.anim) || m.anim == static_cast<uint8_t>(Anim::Jump)) && m.anim != static_cast<uint8_t>(Anim::Roll) && m.step == 1)
            PuppetSfx(actor, PuppetFloorSfx(player, NA_SE_PL_LAND));
        if (s.alive && m.anim == static_cast<uint8_t>(Anim::JumpSlash) && m.step == 1) PuppetSfx(actor, NA_SE_IT_SWORD_SWING_HARD);
        if (s.alive && m.anim == static_cast<uint8_t>(Anim::Drink) && m.step == 1) PuppetVoice(player, NA_SE_VO_LI_DRINK);
    }
    // Footsteps: twice a stride, where the game puts them, sounding like the ground underfoot.
    if (walking && m.jumpT < 0.0f) {
        const float cycle = static_cast<float>(Animation_GetLastFrame(m.seq.step[m.step].anim)) + 1.0f;
        const float cur = player->skelAnime.curFrame;
        for (float at : { cycle * 10.0f / 29.0f, cycle * 24.0f / 29.0f }) {
            const bool crossed = cur >= m.prevFrame ? (m.prevFrame < at && cur >= at) : (at > m.prevFrame || at <= cur);
            if (crossed) PuppetStep(player, m.speed);
        }
        m.prevFrame = cur;
    }
    // Bots' jumps, hops and jump slashes are lifted into the air here (the server only moves them between floors).
    if (m.jumpT >= 0.0f) {
        const bool big = m.anim == static_cast<uint8_t>(Anim::Jump);
        const float seconds = big ? 0.55f : 0.4f, height = big ? 48.0f : m.anim == static_cast<uint8_t>(Anim::JumpSlash) ? 30.0f : 22.0f;
        m.jumpT += 1.0f / (seconds * royale::kTickHz);
        if (m.jumpT >= 1.0f) {
            m.jumpT = -1.0f;
            if (big && s.alive) PuppetSfx(actor, PuppetFloorSfx(player, NA_SE_PL_LAND));
        } else {
            actor->world.pos.y += 4.0f * height * m.jumpT * (1.0f - m.jumpT);
        }
    }
    if (s.anim == static_cast<uint8_t>(royale::Anim::Emote5)) {
        const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
        ApplyChickenDance(player, t);
        actor->world.pos.y += ChickenDanceBob(t);
    }
    // Cancel the animation's own root motion: the network decides where the puppet is.
    Vec3f ignored;
    SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
}

// ---- character skins (tunic colour) -----------------------------------------------------------------------------------------

// The game colours Link's tunic from its cosmetic settings, so a player's skin is applied by setting those just for the moment their
// model is drawn, then putting the local player's own colour back. All three tunic colours are set so it doesn't matter which one is worn.
uint32_t gLocalTunic = royale::SkinRgb(0);
// The selected skin is retained when red/blue gear temporarily replaces it.
uint32_t gLocalWearTunic = gLocalTunic;
uint32_t LocalWearTunic(const royale::HudState& hud) {
    const int slot = static_cast<int>(royale::GearSlot::Tunic);
    if (hud.inv.gearMask & (1 << slot)) {
        const auto item = static_cast<royale::ItemId>(hud.inv.gear[slot].item);
        if (item == royale::ItemId::GoronTunic) return royale::PackRgb(100, 20, 0);
        if (item == royale::ItemId::ZoraTunic) return royale::PackRgb(0, 60, 100);
    }
    return gLocalTunic;
}
bool gTunicApplied = false;                      // we have taken over the cosmetic tunic settings
int gSavedTunicChanged[3] = {0, 0, 0};
Color_RGB8 gSavedTunicValue[3] = {};
const char* const kTunicChanged[3] = { CVAR_COSMETIC("Link.KokiriTunic.Changed"), CVAR_COSMETIC("Link.GoronTunic.Changed"), CVAR_COSMETIC("Link.ZoraTunic.Changed") };
const char* const kTunicValue[3] = { CVAR_COSMETIC("Link.KokiriTunic.Value"), CVAR_COSMETIC("Link.GoronTunic.Value"), CVAR_COSMETIC("Link.ZoraTunic.Value") };

void SetTunicCosmetics(uint32_t rgb) {
    const Color_RGB8 c = { royale::RgbR(rgb), royale::RgbG(rgb), royale::RgbB(rgb) };
    for (int i = 0; i < 3; i++) {
        CVarSetInteger(kTunicChanged[i], 1);
        CVarSetColor24(kTunicValue[i], c);
    }
}

// While you are in a match your own Link wears your chosen skin; leaving gives the player's own cosmetic settings back.
void ApplyLocalTunic(bool on, uint32_t rgb) {
    if (on && !gTunicApplied) {
        for (int i = 0; i < 3; i++) {
            gSavedTunicChanged[i] = CVarGetInteger(kTunicChanged[i], 0);
            gSavedTunicValue[i] = CVarGetColor24(kTunicValue[i], Color_RGB8{ 30, 105, 27 });
        }
        gTunicApplied = true;
    }
    if (on) {
        gLocalWearTunic = rgb;
        SetTunicCosmetics(gLocalWearTunic);
    } else if (gTunicApplied) {
        for (int i = 0; i < 3; i++) {
            CVarSetInteger(kTunicChanged[i], gSavedTunicChanged[i]);
            CVarSetColor24(kTunicValue[i], gSavedTunicValue[i]);
        }
        gTunicApplied = false;
    }
}

void DrawGliderAt(PlayState* play, float x, float y, float z, s16 yaw, float roll, bool diving, uint32_t scheme, bool plain = false, const Player* hanger = nullptr); // with the other custom models, below

// The player being drawn right now if what they hold is the Gilded Sword (the game's sword limbs ask the mod to draw it: OnPlayerCustomSword).
const Player* gGildedPlayer = nullptr;

void DrawVictoryCrown(PlayState* play, const Player* player, uint16_t id);

void Puppet_Draw(Actor* actor, PlayState* play) {
    Feat("other players: draw");
    // Player_Draw reads the local player's equipped item to pick the held model, so show the puppet's own.
    const royale::PuppetState* st = StateOf(actor);
    u8 original = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = st ? PuppetLook(*st).buttonItem : ITEM_NONE;
    if (st && gTunicApplied) SetTunicCosmetics(st->tunic); // this player's own colour
    gGildedPlayer = st != nullptr && st->weapon == royale::ItemId::GildedSword ? reinterpret_cast<const Player*>(actor) : nullptr;
    Player_Draw(actor, play);
    if (st != nullptr) DrawVictoryCrown(play,reinterpret_cast<const Player*>(actor),st->id);
    gGildedPlayer = nullptr;
    if (st && gTunicApplied) SetTunicCosmetics(gLocalWearTunic);
    gSaveContext.equips.buttonItems[0] = original;
    // Everyone who is still in the sky during the drop hangs from a glider.
    if (st && HangingFromGlider(st, actor, play)) {
        DrawGliderAt(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y, 0.0f, false, st->id, st->isBot, (const Player*)actor);   // a bot's glider is the plain model: no cloth to simulate
    }
}

void ForgetCorpse(const Actor* actor); // defined with the other corpse code below

void Puppet_Destroy(Actor* actor, PlayState* play) {
    // The actor was spawned as ACTOR_PLAYER and re-labelled ACTOR_EN_OE2; restore the id so the actor database's
    // per-actor load counter is decremented for the right entry.
    actor->id = ACTOR_PLAYER;
    NameTag_RemoveAllForActor(actor);
    auto it = gPuppetOf.find(actor);
    if (it != gPuppetOf.end()) {
        auto a = gActorOf.find(it->second);
        if (a != gActorOf.end() && a->second == actor) gActorOf.erase(a);
        gPuppetOf.erase(it);
    }
    gPlaying.erase(actor);
    gMotion.erase(actor);
    gPlate.erase(actor);
    ForgetCorpse(actor);
}

void SpawnPuppet(const royale::PuppetState& s) {
    gSpawningPuppet = s.id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, s.x, s.y, s.z, 0, s.rot, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor != nullptr) gActorOf[s.id] = actor;
}

