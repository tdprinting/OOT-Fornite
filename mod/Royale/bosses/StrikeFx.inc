// ---- effects ----------------------------------------------------------------------------------------------------------------------

struct StyleColours { Color_RGBA8 prim, env; };
StyleColours StyleColoursOf(royale::StrikeStyle s) {
    switch (s) {
        case royale::StrikeStyle::Bolt: return { { 210, 225, 255, 255 }, { 90, 120, 255, 255 } };
        case royale::StrikeStyle::Ice: return { { 225, 245, 255, 255 }, { 70, 150, 255, 255 } };
        case royale::StrikeStyle::Water: return { { 170, 230, 255, 255 }, { 0, 110, 255, 255 } };
        case royale::StrikeStyle::Shadow: return { { 200, 110, 255, 255 }, { 50, 0, 90, 255 } };
        case royale::StrikeStyle::Rock: return { { 235, 200, 140, 255 }, { 120, 80, 30, 255 } };
        case royale::StrikeStyle::Magic: return { { 220, 255, 190, 255 }, { 80, 210, 60, 255 } };
        case royale::StrikeStyle::Spore: return { { 210, 255, 130, 255 }, { 80, 160, 20, 255 } };
        case royale::StrikeStyle::Shockwave: return { { 230, 170, 255, 255 }, { 115, 35, 220, 255 } };
        default: return { { 255, 200, 60, 255 }, { 255, 60, 20, 255 } };
    }
}

// Things a strike does on its way down: Phantom Ganon's and Twinrova's blasts fly from their hands, the Lizalfos's spore pods arc through the air,
// rocks fall out of the sky.
bool StrikeFlies(const StrikeFx& s, const BossActor** from) {
    const BossActor* b = BossById(s.owner);
    if (b == nullptr) return false;
    const BK k = KindOf(*b);
    *from = b;
    switch (s.style) {
        case royale::StrikeStyle::Magic: return k == BK::DragonForest;
        case royale::StrikeStyle::Spore: return k == BK::Moss;
        case royale::StrikeStyle::Fire: case royale::StrikeStyle::Ice: return k == BK::DragonSand || (k == BK::DragonFire && ModeOf(*b) == BM::Cast);
        default: return false;
    }
}

void StrikeWarning(PlayState* play, StrikeFx& s, float ground, double now) {
    const float span = static_cast<float>(std::max(0.3, s.land - s.start));
    const float danger = std::clamp(1.0f - static_cast<float>(s.land - now) / span, 0.0f, 1.0f);   // sparks come faster as the blow gets close
    if (s.style == royale::StrikeStyle::Shockwave) return;   // the grenade gives no warning ring: the ball itself is the warning (Projectiles.inc)
    StyleColours c = StyleColoursOf(s.style);
    if (s.style == royale::StrikeStyle::Fire) c.prim.g = static_cast<u8>(210 - danger * 150.0f);
    const int n = 5 + static_cast<int>(danger * 9.0f);
    for (int i = 0; i < n; i++) {   // the ring on the ground, in the strike's colours
        const float a = Rand_ZeroOne() * 6.2831853f;
        Glitter(play, s.x + std::cos(a) * s.radius, ground + 6.0f, s.z + std::sin(a) * s.radius, c.prim, c.env, 1.2f + Rand_ZeroOne() * 1.5f, 25, 32);
    }
    const bool tick = (play->gameplayFrames + static_cast<u32>(s.x)) % 5 == 0;
    switch (s.style) {
        case royale::StrikeStyle::Water: if (tick) Ripple(play, s.x, WaterTop(s.x, s.z, ground), s.z, static_cast<s16>(s.radius * 0.3f), static_cast<s16>(s.radius * 2.0f)); break;
        case royale::StrikeStyle::Shadow: if (tick) Smoke(play, s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 10.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius, { 60, 20, 90, 200 }, 60); break;
        case royale::StrikeStyle::Spore: if (tick) Glitter(play, s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 20.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius, c.prim, c.env, 0.6f, 60, 40); break;
        case royale::StrikeStyle::Ice: if (tick) { Vec3f p = { s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 5.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius }, v = { 0, 2.0f, 0 }, a = { 0, 0, 0 }; Color_RGBA8 pr = { 150, 150, 150, 250 }, en = { 235, 245, 255, 255 }; EffectSsEnIce_Spawn(play, &p, 0.5f, &v, &a, &pr, &en, 25); } break;
        default: break;
    }
    // Flying blasts: a ball of sparks travelling from the boss to where it will land.
    const BossActor* from = nullptr;
    if (StrikeFlies(s, &from)) {
        const float flight = std::min(1.0f, span);
        const float k = 1.0f - static_cast<float>(s.land - now) / flight;
        if (k >= 0.0f && k < 1.0f) {
            const float sy = from->actor->world.pos.y + (royale::IsDragonKind(KindOf(*from)) ? 120.0f : 70.0f);
            const float px = from->x + (s.x - from->x) * k, pz = from->z + (s.z - from->z) * k;
            const float arc = s.style == royale::StrikeStyle::Spore ? std::sin(k * 3.14159f) * 260.0f : 0.0f;
            const float py = sy + (ground + 20.0f - sy) * k + arc;
            for (int i = 0; i < 4; i++) Glitter(play, px + (Rand_ZeroOne() - 0.5f) * 20.0f, py + (Rand_ZeroOne() - 0.5f) * 20.0f, pz + (Rand_ZeroOne() - 0.5f) * 20.0f, c.prim, c.env, 0.0f, 120, 12);
            if (s.style == royale::StrikeStyle::Spore && tick) Smoke(play, px, py, pz, { 120, 200, 60, 160 }, 30);
        }
    }
    // Rocks out of the sky (Volvagia's eruption, Bongo's drumming): one falls onto the spot just before it lands.
    if (s.style == royale::StrikeStyle::Rock && !s.boomed) {
        const BossActor* b = BossById(s.owner);
        const double fall = 0.55;
        if (b && royale::IsDragonKind(KindOf(*b)) && now >= s.land - fall && now < s.land - fall + 0.06) {
            Vec3f p = { s.x, ground + 550.0f, s.z }, v = { 0, -18.0f, 0 }, a = { 0, -1.5f, 0 };
            EffectSsHahen_Spawn(play, &p, &v, &a, 0, 40, -1, 20, nullptr);
            EffectSsHahen_Spawn(play, &p, &v, &a, 0, 25, -1, 20, nullptr);
        }
    }
}

void StrikeBlast(PlayState* play, const StrikeFx& s, float ground) {
    const StyleColours c = StyleColoursOf(s.style);
    // A flash of the strike's own colour lights up whoever is near (a bolt is brighter and whiter).
    if (s.style == royale::StrikeStyle::Bolt) AddLightFlash(s.x, ground + 80.0f, s.z, 200, 215, 255, 600.0f, 0.5f, 3);
    else AddLightFlash(s.x, ground + 40.0f, s.z, c.prim.r, c.prim.g, c.prim.b, std::clamp(s.radius * 2.5f, 220.0f, 520.0f), 0.8f, 6);
    Vec3f pos = { s.x, ground + 20.0f, s.z }, zero = { 0, 0, 0 };
    const BossActor* owner = BossById(s.owner);
    switch (s.style) {
        case royale::StrikeStyle::Bolt:
            gBoltFlashUntil = ImGui::GetTime() + 0.35;
            for (int k = 0; k < 14; k++) {   // the bolt itself: a column of white sparks from the sky
                Glitter(play, s.x + (Rand_ZeroOne() - 0.5f) * 16.0f, ground + 40.0f + k * 55.0f, s.z + (Rand_ZeroOne() - 0.5f) * 16.0f, { 240, 245, 255, 255 }, { 120, 150, 255, 255 }, 0.0f, 260, 10);
            }
            SparkBurst(play, s.x, ground + 20.0f, s.z, { 190, 210, 255, 255 }, 30, 8.0f);
            SoundAt(s.x, ground, s.z, NA_SE_IT_BOMB_EXPLOSION);
            break;
        case royale::StrikeStyle::Ice: {   // shards of ice burst up and out, and a ring of frost
            for (int i = 0; i < 12; i++) {
                const float a = i * 0.5236f;
                Vec3f p = { s.x + std::cos(a) * s.radius * 0.4f, ground + 10.0f, s.z + std::sin(a) * s.radius * 0.4f };
                Vec3f v = { std::cos(a) * 4.0f, 6.0f + Rand_ZeroOne() * 4.0f, std::sin(a) * 4.0f }, acc = { 0, -0.8f, 0 };
                Color_RGBA8 pr = { 150, 150, 150, 250 }, en = { 235, 245, 255, 255 };
                EffectSsEnIce_Spawn(play, &p, 0.9f + Rand_ZeroOne() * 0.6f, &v, &acc, &pr, &en, 30);
            }
            Color_RGBA8 pr = c.prim, en = c.env;
            EffectSsBlast_SpawnShockwave(play, &pos, &zero, &zero, &pr, &en, 10);
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 16, 5.0f);
            SoundAt(s.x, ground, s.z, NA_SE_EN_TWINROBA_MS_FREEZE);
            break;
        }
        case royale::StrikeStyle::Shockwave: {   // the Shockwave Grenade (docs/BOUNCEBALL-MECHANICS.md 3): a purple dome, white bolts, a white flash
            Color_RGBA8 pr = c.prim, en = c.env;
            Vec3f low = { s.x, ground + 4.0f, s.z };
            EffectSsBlast_SpawnShockwave(play, &low, &zero, &zero, &pr, &en, 14);           // the ring flat on the ground
            Color_RGBA8 pr2 = { 250, 235, 255, 255 }, en2 = { 150, 70, 255, 255 };
            EffectSsBlast_SpawnShockwave(play, &pos, &zero, &zero, &pr2, &en2, 9);          // and a brighter, quicker one inside it
            for (int i = 0; i < 18; i++) {   // the dome: sparkles over a half sphere the size of the blast, drifting outward
                const float a = i * (6.2831853f / 18.0f), up = 0.25f + 0.7f * static_cast<float>((i * 7) % 5) / 4.0f;
                const float r = s.radius * 0.55f * std::cos(up), h = s.radius * 0.55f * std::sin(up);
                Glitter(play, s.x + std::cos(a) * r, ground + h, s.z + std::sin(a) * r, { 235, 200, 255, 255 }, { 140, 60, 255, 255 }, 0.4f, 150, 14);
            }
            for (int k = 0; k < 3; k++) {    // white forks of lightning out of the middle, in the game's own lightning
                Vec3f p = { s.x, ground + 30.0f, s.z };
                Color_RGBA8 lp = { 255, 255, 255, 255 }, le = { 170, 90, 255, 255 };
                EffectSsLightning_Spawn(play, &p, &lp, &le, 150, static_cast<s16>(Rand_ZeroOne() * 0xFFFF), 5, 3);
            }
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 24, 7.0f);
            Player* me = GET_PLAYER(play);
            if (me != nullptr && royale::Distance({ me->actor.world.pos.x, me->actor.world.pos.z }, { s.x, s.z }) < s.radius * 2.0f)
                gBoltFlashUntil = std::max(gBoltFlashUntil, ImGui::GetTime() + 0.16);    // a quick white flash for whoever is close (frame 944)
            SoundAt(s.x, ground, s.z, NA_SE_IT_BOMB_EXPLOSION);
            SoundAt(s.x, ground, s.z, NA_SE_EN_FANTOM_THUNDER);
            break;
        }
        case royale::StrikeStyle::Water: {   // a geyser
            Vec3f p = { s.x, WaterTop(s.x, s.z, ground), s.z };
            EffectSsGSplash_Spawn(play, &p, nullptr, nullptr, 0, static_cast<s16>(std::clamp(s.radius * 5.0f, 300.0f, 1000.0f)));
            EffectSsSibuki_SpawnBurst(play, &p);
            Ripple(play, p.x, p.y, p.z, 80, static_cast<s16>(s.radius * 3.0f));
            SoundAt(s.x, ground, s.z, NA_SE_EN_DAIOCTA_SPLASH);
            break;
        }
        case royale::StrikeStyle::Shadow:   // a puff of darkness (and the Dead Hand's hands, drawn by it)
            for (int i = 0; i < 5; i++) Smoke(play, s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 15.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius, { 90, 30, 140, 220 }, 120);
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 14, 4.0f);
            SoundAt(s.x, ground, s.z, owner && KindOf(*owner) == BK::Shade ? NA_SE_EN_DEADHAND_GRIP : NA_SE_EN_SHADEST_LAND);
            break;
        case royale::StrikeStyle::Rock: {   // the ground breaks: rocks thrown up, dust and a shockwave
            EffectSsHahen_SpawnBurst(play, &pos, 10.0f, 0, 14, 8, 10, -1, 10, nullptr);
            Color_RGBA8 pr = { 200, 170, 120, 255 }, en = { 120, 90, 50, 255 };
            EffectSsBlast_SpawnShockwave(play, &pos, &zero, &zero, &pr, &en, 8);
            for (int i = 0; i < 6; i++) {
                const float a = i * 1.0472f;
                Vec3f p = { s.x + std::cos(a) * s.radius * 0.5f, ground + 5.0f, s.z + std::sin(a) * s.radius * 0.5f };
                Vec3f v = { std::cos(a) * 3.0f, 1.0f, std::sin(a) * 3.0f }, acc = { 0, 0, 0 };
                EffectSsDust_Spawn(play, 0, &p, &v, &acc, &pr, &en, 300, 30, 18, 0);
            }
            SoundAt(s.x, ground, s.z, owner && !royale::IsDragonKind(KindOf(*owner)) ? NA_SE_EN_IRONNACK_HIT_GND : NA_SE_EN_VALVAISA_ROCK);
            break;
        }
        case royale::StrikeStyle::Magic: {   // Phantom Ganon's energy: a crackle of lightning and a ring of light
            Color_RGBA8 pr = c.prim, en = c.env;
            EffectSsLightning_Spawn(play, &pos, &pr, &en, 140, Rand_ZeroOne() * 0xFFFF, 6, 3);
            EffectSsBlast_SpawnShockwave(play, &pos, &zero, &zero, &pr, &en, 10);
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 20, 6.0f);
            SoundAt(s.x, ground, s.z, NA_SE_EN_FANTOM_THUNDER_GND);
            break;
        }
        case royale::StrikeStyle::Spore:   // the pod bursts in a cloud of green
            for (int i = 0; i < 4; i++) Smoke(play, s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 20.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius, { 120, 210, 60, 200 }, 110);
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 18, 4.0f);
            SoundAt(s.x, ground, s.z, NA_SE_EN_GOMA_BJR_EGG2);
            break;
        default: {   // fire: the blast, and flames left burning on the ground
            EffectSsBomb2_SpawnLayered(play, &pos, &zero, &zero, static_cast<s16>(std::clamp(s.radius * 0.6f, 40.0f, 110.0f)), 14);
            SparkBurst(play, s.x, ground + 20.0f, s.z, { 255, 190, 60, 255 }, 26, 7.0f);
            const int flames = s.radius > 100.0f ? 4 : 2;
            for (int i = 0; i < flames; i++) {
                Vec3f p = { s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground, s.z + (Rand_ZeroOne() - 0.5f) * s.radius };
                EffectSsGFire_Spawn(play, &p);
            }
            SoundAt(s.x, ground, s.z, NA_SE_IT_BOMB_EXPLOSION);
            break;
        }
    }
}

// Each boss's own effects while it moves: fire breath, embers, frost, bubbles and ripples, dirt where something burrows, sparks, stars when dazed.
void UpdateBossBodyFx(PlayState* play) {
    const float t = BossTime(play);
    for (auto& [id, b] : gBosses) {
        if (!b.actor) continue;
        const BM mode = ModeOf(b);
        const float ang = b.rot * (3.14159265f / 32768.0f), fx = std::sin(ang), fz = std::cos(ang);
        const float ground = GroundY(play, b.x, b.z, b.actor->world.pos.y);
        const float y = b.actor->world.pos.y;
        b.fxClock += 1.0f / royale::kTickHz;
        const bool every = static_cast<int>(b.fxClock * 20.0f) % 4 == 0;
        if (royale::IsChuKind(KindOf(b)) && every) {
            if ((KindOf(b) == BK::ChuYellow || KindOf(b) == BK::ChuBlue) && (b.aux == 1 || mode == BM::Summon)) {
                Vec3f at = {b.x, y + 100.0f, b.z};
                Color_RGBA8 prim = {235,255,120,255}, env = {80,210,255,255};
                EffectSsLightning_Spawn(play, &at, &prim, &env, 110, Rand_ZeroOne() * 0xFFFF, 6, 3);
            }
            if (KindOf(b) == BK::ChuRed) Glitter(play,b.x,y+80,b.z,{255,160,35,255},{180,40,0,255},1.2f,35,14);
            if (KindOf(b) == BK::ChuDark && mode == BM::Stunned && b.aux == 4)
                Glitter(play,b.x,y+120,b.z,{255,255,200,255},{220,210,140,255},.3f,40,14);
        }
        auto breath = [&](float reach, float height, Color_RGBA8 prim, int count, float sp0) {   // a stream of sparks out of its mouth
            Color_RGBA8 env = { 255, 255, 255, 255 };
            for (int i = 0; i < count; i++) {
                const float spread = (Rand_ZeroOne() - 0.5f) * 0.45f, sp = sp0 + Rand_ZeroOne() * sp0 * 0.9f;
                const float dx = fx * std::cos(spread) - fz * std::sin(spread), dz = fz * std::cos(spread) + fx * std::sin(spread);
                Vec3f pos = { b.x + fx * reach, y + height, b.z + fz * reach };
                Vec3f vel = { dx * sp, -sp * 0.2f, dz * sp };
                Vec3f accel = { 0.0f, -0.3f, 0.0f };
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 30, 70);
            }
        };
        auto dazed = [&](float height) {   // stars circling its head
            for (int i = 0; i < 2; i++) {
                const float a = t * 5.0f + i * 3.14159f;
                Glitter(play, b.x + std::cos(a) * 45.0f, y + height, b.z + std::sin(a) * 45.0f, { 255, 255, 150, 255 }, { 255, 200, 0, 255 }, 0.0f, 60, 8);
            }
        };
        switch (KindOf(b)) {
            case BK::Stone:
                if (mode == BM::Stunned && b.aux == 3 && every) Glitter(play, b.x + (Rand_ZeroOne() - 0.5f) * 80.0f, ground + 10.0f, b.z + (Rand_ZeroOne() - 0.5f) * 80.0f, { 255, 60, 40, 255 }, { 180, 0, 0, 255 }, 2.0f, 60, 20);
                break;
            case BK::Lava:   // embers drift off its molten hide; fire out of its mouth; a roll throws up sparks
                if (every) Glitter(play, b.x + (Rand_ZeroOne() - 0.5f) * 90.0f, y + 40.0f + Rand_ZeroOne() * 60.0f, b.z + (Rand_ZeroOne() - 0.5f) * 90.0f, { 255, 140, 30, 255 }, { 255, 40, 0, 255 }, 1.6f, 40, 25);
                if (mode == BM::Breath && b.modeAge > 0.25f) breath(85.0f * MiniScale(b.kind) / 0.033f, 45.0f, { 255, 120, 30, 255 }, 10, 14.0f);
                if (mode == BM::Charge) SparkBurst(play, b.x, ground + 10.0f, b.z, { 255, 120, 30, 255 }, 2, 3.0f);
                if (mode == BM::Stunned) dazed(110.0f);
                break;
            case BK::Frost:   // frosty breath, and a cold glitter
                if (every) Glitter(play, b.x + fx * 50.0f, y + 60.0f, b.z + fz * 50.0f, { 230, 245, 255, 255 }, { 120, 180, 255, 255 }, 0.4f, 40, 20);
                if (mode == BM::Summon) {   // the howl: frost spreading out around it
                    const float r = 60.0f + b.modeAge * 380.0f;
                    for (int i = 0; i < 6; i++) { const float a = Rand_ZeroOne() * 6.2831853f; Glitter(play, b.x + std::cos(a) * r, ground + 8.0f, b.z + std::sin(a) * r, { 230, 245, 255, 255 }, { 80, 160, 255, 255 }, 1.0f, 50, 20); }
                }
                break;
            case BK::Moss:   // the odd spore drifting off it, so you can just about spot it
                if (every && b.fade > 0.05f && Rand_ZeroOne() < 0.5f) Glitter(play, b.x + (Rand_ZeroOne() - 0.5f) * 60.0f, y + 40.0f + Rand_ZeroOne() * 50.0f, b.z + (Rand_ZeroOne() - 0.5f) * 60.0f, { 200, 255, 120, 200 }, { 60, 140, 20, 255 }, 0.5f, 30, 25);
                break;
            case BK::Tide:   // a wake of ripples while it swims, spray while it spins, stars when it is dizzy
                if ((b.aux == 1 || b.moved > 2.0f) && every) Ripple(play, b.x, WaterTop(b.x, b.z, ground), b.z, 60, 300);
                if (mode == BM::Charge) { Vec3f p = { b.x, ground + 30.0f, b.z }; if (every) EffectSsSibuki_SpawnBurst(play, &p); }
                if (mode == BM::Stunned) dazed(150.0f);
                break;
            case BK::Shade:   // the dirt churning where it moves under the ground
                if (mode == BM::Hidden && every) {
                    Vec3f p = { b.x, ground + 5.0f, b.z }, v = { 0, 2.0f, 0 }, a = { 0, 0, 0 };
                    Color_RGBA8 pr = { 130, 100, 70, 255 }, en = { 70, 50, 30, 255 };
                    EffectSsDust_Spawn(play, 0, &p, &v, &a, &pr, &en, 200, 20, 14, 0);
                    EffectSsHahen_Spawn(play, &p, &v, &a, 0, 6, -1, 10, nullptr);
                }
                break;
            case BK::Dune:
                if (mode == BM::Stunned && every) { Vec3f p = { b.x + fx * 120.0f, ground + 5.0f, b.z + fz * 120.0f }, v = { 0, 1.0f, 0 }, a = { 0, 0, 0 }; Color_RGBA8 pr = { 220, 190, 130, 255 }, en = { 140, 110, 60, 255 }; EffectSsDust_Spawn(play, 0, &p, &v, &a, &pr, &en, 150, 15, 12, 0); }
                if (b.armourOff && every) Glitter(play, b.x, y + 90.0f, b.z, { 255, 220, 120, 255 }, { 255, 120, 0, 255 }, 1.5f, 30, 15);   // steaming with fury
                break;
            case BK::DragonFire:   // lava welling up where it is under the ground; fire out of its mouth
                if (mode == BM::Hidden) {
                    if (every) { Vec3f p = { b.x, ground, b.z }; EffectSsGMagma_Spawn(play, &p); }
                    SparkBurst(play, b.x, ground + 10.0f, b.z, { 255, 120, 30, 255 }, 1, 4.0f);
                }
                if (mode == BM::Breath) breath(190.0f, 120.0f, { 255, 120, 40, 255 }, 14, 20.0f);
                break;
            case BK::DragonWater: {   // ripples and bubbles where it moves under the water; drips off the tentacle
                const float water = WaterTop(b.x, b.z, ground);
                if (mode == BM::Hidden) {
                    if (every) Ripple(play, b.x, water, b.z, 80, 500);
                    if (Rand_ZeroOne() < 0.3f) { Vec3f p = { b.x, water, b.z }; EffectSsBubble_Spawn(play, &p, 0.0f, 10.0f, 60.0f, 0.06f); }
                } else if (every) {
                    Ripple(play, b.x, water, b.z, 150, 600);
                    if (mode == BM::Slam) { Vec3f p = { b.x + fx * 200.0f, water + 40.0f, b.z + fz * 200.0f }; EffectSsSibuki_SpawnBurst(play, &p); }
                }
                break;
            }
            case BK::DragonForest:   // a trail of dark energy when he charges; the storm gathering in his hands
                if (mode == BM::Charge) SparkBurst(play, b.x, y + 80.0f, b.z, { 180, 90, 255, 255 }, 3, 2.0f);
                if (mode == BM::Cast || mode == BM::Beam) Glitter(play, b.x + fx * 40.0f, y + 150.0f, b.z + fz * 40.0f, { 230, 255, 200, 255 }, { 90, 220, 60, 255 }, 0.0f, 90, 10);
                if (mode == BM::Stunned) dazed(220.0f);
                break;
            case BK::DragonShadow:   // shadowy mist where he is, seen or not; a shockwave off every beat of his drum
                if (every) Smoke(play, b.x + (Rand_ZeroOne() - 0.5f) * 200.0f, ground + 40.0f, b.z + (Rand_ZeroOne() - 0.5f) * 200.0f, { 60, 20, 90, mode == BM::Hidden ? (u8)140 : (u8)90 }, 160);
                if (mode == BM::Summon && static_cast<int>(b.modeAge * 9.0f / 3.14159f) != static_cast<int>((b.modeAge - 0.05f) * 9.0f / 3.14159f)) {
                    for (int h = 0; h < 2; h++) {
                        const Vec3f at = BongoHandAt(play, b, id, h);
                        Vec3f p = { at.x, ground + 10.0f, at.z }, zero = { 0, 0, 0 };
                        EffectSsBlast_SpawnWhiteShockwave(play, &p, &zero, &zero);
                    }
                    BossSound(b, static_cast<int>(b.modeAge * 9.0f / 3.14159f) & 1 ? NA_SE_EN_SHADEST_TAIKO_HIGH : NA_SE_EN_SHADEST_TAIKO_LOW);
                }
                break;
            case BK::DragonSand: {   // Koume's fire on one side of her, Kotake's ice on the other
                const float rx = std::cos(ang), rz = -std::sin(ang);
                Glitter(play, b.x + rx * 70.0f, y + 150.0f, b.z + rz * 70.0f, { 255, 170, 40, 255 }, { 255, 40, 0, 255 }, 1.2f, 70, 14);
                Glitter(play, b.x - rx * 70.0f, y + 150.0f, b.z - rz * 70.0f, { 220, 240, 255, 255 }, { 60, 140, 255, 255 }, 1.2f, 70, 14);
                if (mode == BM::Charge || mode == BM::Swoop) SparkBurst(play, b.x - fx * 60.0f, y + 40.0f, b.z - fz * 60.0f, b.aux == 1 ? Color_RGBA8{ 200, 230, 255, 255 } : Color_RGBA8{ 255, 160, 60, 255 }, 3, 2.0f);
                if (mode == BM::Stunned) dazed(260.0f);
                break;
            }
            default: break;
        }
    }
}

// Every game frame: the strikes on the ground and each boss's own effects.
void UpdateBossWorldFx() {
    if (!InField() || gPlayState == nullptr) { gStrikeFx.clear(); return; }
    const double now = ImGui::GetTime();
    Player* pl = GET_PLAYER(gPlayState);
    for (StrikeFx& s : gStrikeFx) {
        const float ground = GroundY(gPlayState, s.x, s.z, pl->actor.world.pos.y);
        if (now < s.land) StrikeWarning(gPlayState, s, ground, now);
        else if (!s.boomed) { s.boomed = true; StrikeBlast(gPlayState, s, ground); }
    }
    gStrikeFx.erase(std::remove_if(gStrikeFx.begin(), gStrikeFx.end(), [&](const StrikeFx& s) { return now > s.land + 1.0; }), gStrikeFx.end());
    UpdateBossBodyFx(gPlayState);
}

// The sound everyone hears when a major boss arrives.
u16 BossArrivalSound(int kind) {
    switch (static_cast<BK>(kind)) {
        case BK::DragonWater: return NA_SE_EN_MOFER_APPEAR;
        case BK::DragonForest: return NA_SE_EN_FANTOM_LAUGH;
        case BK::DragonShadow: return NA_SE_EN_SHADEST_TAIKO_HIGH;
        case BK::DragonSand: return NA_SE_EN_TWINROBA_LAUGH;
        default: return NA_SE_EN_VALVAISA_ROAR;
    }
}

// A strike the server just announced.
void AddStrikeFx(float x, float z, float radius, float delay, uint8_t style, uint16_t owner) {
    const double now = ImGui::GetTime();
    const royale::StrikeStyle st = style < static_cast<uint8_t>(royale::StrikeStyle::Count) ? static_cast<royale::StrikeStyle>(style) : royale::StrikeStyle::Fire;
    uint32_t id = 0;
    for (const auto& [bid, b] : gBosses) if (static_cast<uint16_t>(bid) == owner) id = bid;
    gStrikeFx.push_back({ x, z, radius, now, now + std::max(0.3f, delay), false, st, id });
}
