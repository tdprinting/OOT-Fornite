// ---- water ---------------------------------------------------------------------------------------------------------------------------
// The game's own water is a flat, scrolling-texture sheet. This adds, on top of every water box (lakes, rivers) and the Fortnite map's sea:
//   * splashes, ripples and bubbles made with the game's own effects (EffectSsGSplash, EffectSsGRipple, EffectSsBubble: the very sprites Link
//     makes) for everyone but the local player, whom the game already splashes itself; rings falling on the water in the rain;
//   * displacement: swimmers, bots and carts push the surface down under them and ahead of them, and leave rings that spread out and fade,
//     with a trail of foam, all in a height field that also rolls with gentle swell (bigger in wind and storms);
//   * a light, translucent surface over the game's (finer near the camera) that is tinted by depth, foams in the shallows, and shows the
//     sky in it (stronger the lower you look: a Fresnel curve) and a glitter path for the sun and the moon;
//   * a stylised reflection of anyone standing or swimming at the water: a smeared, wobbling strip in their colours, mirrored over the surface
//     toward the camera. It is not a true mirror image of the scene (the N64 renderer has no render-to-texture), and trees and cliffs are not mirrored;
//   * a gentle current that carries a swimming player, and an underwater look (blue tint, light shafts) when the camera is below the surface.
// Everything is cosmetic and local (nothing is sent), apart from the swimmer's own drift. Debug switch "Water"; the Graphics page has the rest.
bool WaterSurfaceAt(float x, float z, float* y) {
    namespace fn = royale::fortnite;
    if (OnIsland()) {
        if (std::fabs(x) >= fn::kHalfX || std::fabs(z) >= fn::kHalfZ) return false;
        *y = OnRiftlandsTerrain() ? royale::RiftlandsWaterY({x,z}) : static_cast<float>(fn::kWaterY);
        return true;
    }
    float s = 0;
    WaterBox* box = nullptr;
    if (WaterBox_GetSurface1(gPlayState, &gPlayState->colCtx, x, z, &s, &box) == 0) return false;
    *y = s;
    return true;
}


float WaveAmp() {
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    return gWaterWaves * (0.8f + 1.1f * std::clamp(wind, 0.0f, 1.0f));
}

// Daylight on the water, 0.38 at night to 1 at noon (the game's own clock).
float WaterLight() {
    const float f = std::sin((static_cast<float>(gSaveContext.dayTime) - 16384.0f) / 32768.0f * 3.14159265f);
    return 0.38f + 0.62f * std::clamp(f * 1.4f, 0.0f, 1.0f);
}

// The current, in units a second: a slow wandering flow that differs from place to place, plus a little of the wind.
void WaterCurrentAt(float x, float z, float t, float* cx, float* cz) {
    const float a = std::sin(x * 0.0012f + t * 0.11f) + 0.8f * std::cos(z * 0.0009f - t * 0.07f);
    const float ang = a * 1.7f + 0.6f;
    const float m = 9.0f + 5.0f * std::sin(z * 0.0006f + x * 0.0004f);
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    *cx = std::cos(ang) * m + wx * 0.1f;
    *cz = std::sin(ang) * m + wz * 0.1f;
}

// ---- displacement: rings that spread over the surface, and the dip and bow wave under and ahead of whoever is in it
struct WaterDisturb { float x, z, birth, strength, size; };
std::vector<WaterDisturb> gWaterDist;
constexpr size_t kMaxWaterDist = 40;
struct WaterBodyNow { float x, z, size, dx, dz, speed; };
std::vector<WaterBodyNow> gWaterBodies;
struct WaterRefl { float x, z, surface, h, w; float c1[3], c2[3]; };
std::vector<WaterRefl> gWaterRefl;
struct WaterTrack { float x = 0, y = 0, z = 0, ring = 0, spawn = 0, dist = 0; bool in = false; int seen = 0; };
std::unordered_map<const void*, WaterTrack> gWaterTrack;
int gWaterFrame = 0, gWaterBudget = 0;
float gCamUnder = 0.0f;   // 0 above the surface .. 1 below it (eased), for the underwater overlay
float gCamDepth = 0.0f;   // how far under the surface the camera is (units)
float gWaterNow = 0.0f;   // the clock the displacement runs on (seconds)
// The ripple simulation round the player (shared/water_sim.h): 64 x 64 cells of 14 units. Rings spread, cross, mix and fade at its rim.
royale::water::RippleField gRipples(14.0f, 120.0f, 0.55f);

void AddWaterDisturb(float x, float z, float strength, float size) {
    if (gWaterRipples && gRipples.Contains(x, z, gRipples.Cell() * 8.0f)) {   // near the player the simulation makes the ring
        gRipples.Impulse(x, z, strength * size * 0.45f, size * 0.9f);
        return;
    }
    if (gWaterDist.size() >= kMaxWaterDist) gWaterDist.erase(gWaterDist.begin());
    gWaterDist.push_back({ x, z, gWaterNow, strength, size });
}

constexpr float kWaterRingSpeed = 90.0f;   // how fast a ring spreads, in units a second
constexpr float kWaterRingLife = 4.5f;     // and how long it lasts

// How far the displacement lifts the surface here (it can be negative), and how much foam is on it (0 to 1).
float WaterDisplaceAt(float x, float z, float* foam) {
    float h = 0.0f, f = 0.0f;
    for (const WaterDisturb& d : gWaterDist) {
        const float dx = x - d.x, dz = z - d.z, r2 = dx * dx + dz * dz, a = gWaterNow - d.birth;
        const float front = kWaterRingSpeed * a, band = 26.0f + d.size * 1.3f;
        const float reach = front + band * 2.5f + d.size * 2.0f;
        if (r2 > reach * reach) continue;
        const float r = std::sqrt(r2), off = r - front, env = std::exp(-(off * off) / (band * band));
        const float life = std::max(0.0f, 1.0f - a / kWaterRingLife);   // the ring dies away smoothly (it is dropped when its time is up, so it must not stop short)
        h += d.strength * d.size * 0.28f * env * std::exp(-a * 0.55f) * life / (1.0f + r / (d.size * 6.0f)) * std::cos(off * 0.09f);
        if (a < 1.6f) f += d.strength * 0.7f * std::exp(-r2 / (d.size * d.size * 1.5f)) * (1.0f - a / 1.6f);
        f += d.strength * 0.3f * env * life * (0.5f + 0.5f * std::cos(off * 0.09f)) / (1.0f + r / (d.size * 8.0f));   // a pale crest on the front of the ring
    }
    for (const WaterBodyNow& b : gWaterBodies) {
        const float dx = x - b.x, dz = z - b.z, r2 = dx * dx + dz * dz, s2 = b.size * b.size;
        if (r2 > s2 * 40.0f) continue;
        h -= b.size * 0.3f * std::exp(-r2 / (s2 * 3.0f));   // the dip under it
        const float run = std::min(1.0f, b.speed / 120.0f);
        if (run > 0.05f) {
            const float bx = x - (b.x + b.dx * b.size * 1.6f), bz = z - (b.z + b.dz * b.size * 1.6f);
            h += b.size * 0.2f * run * std::exp(-(bx * bx + bz * bz) / (s2 * 2.0f));   // the bow wave ahead
            const float tx = x - (b.x - b.dx * b.size * 1.4f), tz = z - (b.z - b.dz * b.size * 1.4f);
            f += 0.45f * run * std::exp(-(tx * tx + tz * tz) / (s2 * 2.5f));   // and foam in its wake
        }
    }
    if (gWaterRipples) {
        h += gRipples.Sample(x, z);
        float sx, sz;
        gRipples.Slope(x, z, &sx, &sz);
        f += std::clamp((std::hypot(sx, sz) - 0.12f) * 1.5f, 0.0f, 0.5f);   // steep ripples break into a little foam
    }
    *foam = std::min(1.0f, f);
    return h;
}

// ---- for creatures, boats and water physics to come: the water at a point, as it is drawn ------------------------------------------------
// `height` is where the surface is now (the level, plus the swell and the ripples); `depth` how deep the water is there; (nx, ny, nz) the
// surface's normal; (flowX, flowZ) the current in units a second. Push the water with AddWaterDisturb (a ring) or gRipples.Impulse.
struct WaterInfo { bool water = false; float surface = 0, height = 0, depth = 0, nx = 0, ny = 1, nz = 0, flowX = 0, flowZ = 0; };
[[maybe_unused]] WaterInfo WaterInfoAt(float x, float z) {
    WaterInfo w;
    if (gPlayState == nullptr || !WaterSurfaceAt(x, z, &w.surface)) return w;
    w.water = true;
    float floorY = 0;
    w.depth = RawFloorAt(x, z, &floorY) ? w.surface - floorY : 1000.0f;
    const float shore = std::min(1.0f, std::max(0.0f, w.depth) / 90.0f);
    const royale::water::SwellPoint sp = royale::water::Swell(x, z, gWaterNow, WaveAmp() * (OnIsland() ? 1.0f : 0.35f), gWaterChop);
    float foam = 0;
    w.height = w.surface + (sp.h + WaterDisplaceAt(x, z, &foam)) * shore;
    w.nx = sp.nx * shore; w.ny = sp.ny; w.nz = sp.nz * shore;
    const float l = std::sqrt(w.nx * w.nx + w.ny * w.ny + w.nz * w.nz);
    w.nx /= l; w.ny /= l; w.nz /= l;
    WaterCurrentAt(x, z, gWaterNow, &w.flowX, &w.flowZ);
    return w;
}

// ---- the game's own effects (the sprites Link makes), kept to a few a frame so the effect table is never swamped
void WaterRipple(float x, float z, float surface, float scale, int delay) {
    if (!gWaterFxOn || gWaterBudget <= 0) return;
    gWaterBudget--;
    Vec3f p = { x, surface, z };
    EffectSsGRipple_Spawn(gPlayState, &p, static_cast<s16>(100.0f * scale), static_cast<s16>(std::min(30000.0f, 500.0f * scale)), static_cast<s16>(delay));
}

void WaterSplashFx(float x, float z, float surface, float size, float strength) {
    if (!gWaterFxOn || gWaterBudget <= 1) return;
    gWaterBudget -= 2;
    Vec3f p = { x, surface, z };
    const s16 scale = static_cast<s16>(std::clamp(550.0f * (size / 16.0f) * (0.5f + strength), 250.0f, 3200.0f));
    EffectSsGSplash_Spawn(gPlayState, &p, nullptr, nullptr, strength > 0.9f ? 1 : 0, scale);   // type 1 throws a second, smaller splash after it
    WaterRipple(x, z, surface, size / 16.0f, 0);
}

// ---- more water life, from the game's own sprites: the bubble sprite (EffectSsDtBubble, which rises and fades), the spray droplets Gohma throws
// (EffectSsSibuki: the same bubble sprite flung in an arc), the splash and the ripple. The game's effect table is small, so a running estimate of
// how many bubbles are alive limits them, and each frame has a small budget of its own.
float gBubbleLoad = 0.0f;   // roughly how many of our bubbles are alive (counted as they are made, drained over time)
int gBubbleBudget = 0;
constexpr float kBubbleCap = 44.0f;

bool BubbleRoom(float cost) {
    if (!gWaterFxOn || !gWaterLife || gBubbleBudget <= 0 || gBubbleLoad + cost > kBubbleCap || gPlayState == nullptr) return false;
    gBubbleBudget--;
    gBubbleLoad += cost;
    return true;
}

// One bubble that rises from (x, y, z) at `rise` units a frame for `life` frames, then fades out. `scale` ~ 24 (small) to 60 (big).
void WaterBubble(float x, float y, float z, float rise, int life, int scale, bool wander = true) {
    if (!BubbleRoom(std::max(1.0f, life / 30.0f))) return;
    static Color_RGBA8 prim = { 225, 248, 255, 215 }, env = { 80, 170, 225, 255 };
    Vec3f p = { x, y, z }, vel = { 0.0f, rise, 0.0f }, acc = { 0.0f, 0.02f, 0.0f };
    EffectSsDtBubble_SpawnCustomColor(gPlayState, &p, &vel, &acc, &prim, &env, static_cast<s16>(scale), static_cast<s16>(life), wander ? 1 : 0);
}

// A burst of bubbles from a body that has gone under: a fountain of them, each a little different, climbing at their own pace.
void WaterBubbleBurst(float x, float y, float z, float surface, int count, float spread) {
    for (int k = 0; k < count; k++) {
        const float a = Flora01(gWaterFrame, k, 640) * 6.2831853f, r = spread * Flora01(gWaterFrame, k, 641);
        const float by = y + 10.0f * Flora01(gWaterFrame, k, 642), rise = 1.6f + 1.8f * Flora01(gWaterFrame, k, 643);
        const int life = std::clamp(static_cast<int>((surface - by) / rise) - 1, 6, 70);
        WaterBubble(x + std::cos(a) * r, by, z + std::sin(a) * r, rise, life, 26 + static_cast<int>(30.0f * Flora01(gWaterFrame, k, 644)));
    }
}

// Spray thrown off the surface (the game's droplets, flung sideways across the camera, up and back down).
void WaterSpray(float x, float surface, float z, int count) {
    for (int k = 0; k < count; k++) {
        if (!BubbleRoom(0.4f)) return;
        Vec3f p = { x + (Flora01(gWaterFrame, k, 650) - 0.5f) * 14.0f, surface + 2.0f, z + (Flora01(gWaterFrame, k, 651) - 0.5f) * 14.0f }, vel = { 0, 0, 0 }, acc = { 0, 0, 0 };
        EffectSsSibuki_Spawn(gPlayState, &p, &vel, &acc, static_cast<s16>(k % 3), static_cast<s16>(k & 1), static_cast<s16>(14 + 14.0f * Flora01(gWaterFrame, k, 652)));
    }
}

// A small bubble that lies on the surface a moment and fades: the foam bubbles of a wake.
void WaterFoamBubble(float x, float surface, float z, float size) {
    WaterBubble(x + (Flora01(gWaterFrame, 3, 660) - 0.5f) * size, surface + 1.5f, z + (Flora01(gWaterFrame, 4, 661) - 0.5f) * size, 0.0f, 14, 14 + static_cast<int>(10.0f * Flora01(gWaterFrame, 5, 662)));
}

// The life of the water itself, round the player: vents on the sea floor breathe out strings of bubbles that pop in rings at the surface, and now and
// then something leaps out of the water (a splash, spray and rings). Fixed spots from the map, so everyone sees the same vents.
void WaterLife(PlayState* play, float dt, bool paused) {
    if (!gWaterLife || !gWaterFxOn || paused) return;
    Player* pl = GET_PLAYER(play);
    if (pl == nullptr) return;
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;
    constexpr float kVentCell = 640.0f;
    const int vcx = static_cast<int>(std::floor(px / kVentCell)), vcz = static_cast<int>(std::floor(pz / kVentCell));
    // one vent is tried each frame (the ones that exist are always the same; which is tried is by the frame)
    const int ox = static_cast<int>(Flora01(gWaterFrame, 1, 671) * 5.0f) - 2, oz = static_cast<int>(Flora01(gWaterFrame, 2, 672) * 5.0f) - 2;
    const int cx = vcx + ox, cz = vcz + oz;
    if (Flora01(cx, cz, 673) < 0.34f) {
        const float x = (cx + 0.15f + 0.7f * Flora01(cx, cz, 674)) * kVentCell, z = (cz + 0.15f + 0.7f * Flora01(cx, cz, 675)) * kVentCell;
        float surface = 0, floorY = 0;
        if (WaterSurfaceAt(x, z, &surface) && RawFloorAt(x, z, &floorY) && surface - floorY > 70.0f && surface - floorY < 700.0f && std::hypot(x - px, z - pz) < 1300.0f) {
            // a vent breathes in puffs: on for a couple of seconds, off for a few
            const float phase = std::fmod(gWaterNow * 0.22f + Flora01(cx, cz, 676), 1.0f);
            if (phase < 0.45f) {
                const float by = std::max(floorY, surface - 520.0f) + 4.0f;
                const float rise = 2.0f + 0.8f * Flora01(cx, cz, 677);
                const int life = std::clamp(static_cast<int>((surface - by) / rise) - 1, 8, 80);
                WaterBubble(x, by, z, rise, life, 28 + static_cast<int>(26.0f * Flora01(gWaterFrame, 9, 678)));
                if (Flora01(gWaterFrame, 10, 679) < 0.3f) { gWaterBudget++; WaterRipple(x, z, surface, 0.22f, life); }   // it pops at the surface in a ring, when the bubble gets there
            }
        }
    }
    // something leaps: about once in four seconds, somewhere in view
    if (Flora01(gWaterFrame, 11, 680) < dt * 0.25f) {
        const float a = Flora01(gWaterFrame, 12, 681) * 6.2831853f, r = 350.0f + 1100.0f * Flora01(gWaterFrame, 13, 682);
        const float x = px + std::cos(a) * r, z = pz + std::sin(a) * r;
        float surface = 0, floorY = 0;
        if (WaterSurfaceAt(x, z, &surface) && (!RawFloorAt(x, z, &floorY) || surface - floorY > 60.0f)) {
            const float rw = std::clamp(Flora01(gWaterFrame, 14, 683), 0.0f, 1.0f);
            WaterSplashFx(x, z, surface, 10.0f + 6.0f * rw, 0.5f);
            WaterSpray(x, surface, z, 4);
            WaterBubbleBurst(x, surface - 30.0f, z, surface, 3, 14.0f);
            if (gWaterBudget > 0) WaterRipple(x, z, surface, 0.35f, 8);
        }
    }
}

// One body in the water this frame. The local player is splashed, rippled and bubbled by the game itself; everyone else gets the same
// effects from here. All of them push the surface (displacement) and leave foam.
void WaterBody(const void* key, float x, float y, float z, float size, float dt, bool local) {
    WaterTrack& t = gWaterTrack[key];
    float surface = 0, floorY = 0;
    const bool have = WaterSurfaceAt(x, z, &surface);
    const bool in = have && y < surface + 8.0f && (!RawFloorAt(x, z, &floorY) || floorY < surface - 4.0f);
    const float mx = x - t.x, mz = z - t.z, my = y - t.y;
    const bool jumped = t.seen == 0 || std::hypot(mx, mz) > 500.0f || std::fabs(my) > 400.0f || dt <= 0.0001f;
    const float speed = jumped ? 0.0f : std::hypot(mx, mz) / dt;
    const float eyeD = std::hypot(x - gPlayState->view.eye.x, z - gPlayState->view.eye.z);
    if (!jumped && eyeD < 4500.0f) {
        if (in) {
            const float inv = 1.0f / std::max(1.0f, speed), dx = mx / dt * inv, dz = mz / dt * inv;
            if (gWaterWakes) gWaterBodies.push_back({ x, z, size, speed > 8.0f ? dx : 0.0f, speed > 8.0f ? dz : 0.0f, speed });
            if (!t.in) {   // going in
                const float strength = std::clamp(0.35f + std::max(0.0f, -my / dt) / 500.0f + speed / 400.0f, 0.3f, 1.5f);
                if (gWaterWakes) AddWaterDisturb(x, z, strength * 1.2f, size);
                if (!local) WaterSplashFx(x, z, surface, size, strength);
                WaterSpray(x, surface, z, std::clamp(static_cast<int>(2.0f + 5.0f * strength), 2, 8));   // spray off the surface as it goes in...
                WaterBubbleBurst(x, std::max(y, surface - 60.0f), z, surface, std::clamp(static_cast<int>(3.0f + 6.0f * strength), 3, 10), size * 0.8f);   // ...and the air it takes down
            } else {
                t.ring -= dt;
                t.spawn -= dt;
                if (speed > 25.0f) {
                    if (t.spawn <= 0.0f) {   // a ring left in the water every so often along the way
                        t.spawn = std::clamp(0.30f - speed / 1500.0f, 0.1f, 0.3f);
                        if (gWaterWakes) AddWaterDisturb(x - dx * size * 0.4f, z - dz * size * 0.4f, std::min(1.0f, 0.25f + speed / 250.0f), size);
                    }
                    if (!local && t.ring <= 0.0f) {
                        t.ring = std::clamp(0.45f - speed / 900.0f, 0.12f, 0.45f);
                        WaterRipple(x, z, surface, size / 16.0f, 0);
                        if (speed > 150.0f) WaterSplashFx(x - dx * size * 0.4f, z - dz * size * 0.4f, surface, size * 0.6f, 0.25f);   // fast (a cart, a sprint): spray off the bow
                    }
                } else if (t.ring <= 0.0f) {   // treading water
                    t.ring = 0.9f;
                    if (gWaterWakes) AddWaterDisturb(x, z, 0.35f, size * 0.8f);
                    if (!local) WaterRipple(x, z, surface, size / 16.0f * 0.8f, 0);
                }
                if (y + 38.0f < surface && gWaterFxOn) {   // someone under water: a steady string of bubbles (the game's own, and ours that rise and fade)
                    if (!local && gWaterBudget > 0 && Flora01(gWaterFrame, 7, 504) < dt * 2.0f) {
                        gWaterBudget--;
                        Vec3f p = { x, y, z };
                        EffectSsBubble_Spawn(gPlayState, &p, 20.0f, 10.0f, 20.0f, 0.13f);
                    }
                    if (Flora01(gWaterFrame, 8, 505) < dt * (local ? 6.0f : 8.0f)) WaterBubbleBurst(x, y + size * 1.5f, z, surface, 1 + (speed > 60.0f ? 1 : 0), size * 0.5f);
                } else if (speed > 40.0f && Flora01(gWaterFrame, 9, 506) < dt * 7.0f) {   // swimming at the top: foam bubbles in the wake
                    WaterFoamBubble(x - dx * size * 0.8f, surface, z - dz * size * 0.8f, size * 1.4f);
                    if (speed > 150.0f && Flora01(gWaterFrame, 10, 507) < 0.5f) WaterSpray(x + dx * size, surface, z + dz * size, 2);   // fast: spray off the bow
                }
            }
            if (gWaterRefl.size() < 12 && gWaterSkyOn && gWaterBodiesRefl && y + size * 3.4f > surface + 6.0f) {
                const bool cart = size > 30.0f;
                WaterRefl r;
                r.x = x; r.z = z; r.surface = surface; r.h = std::min(size * (cart ? 1.4f : 3.6f), y + size * (cart ? 1.4f : 3.6f) - surface); r.w = size * (cart ? 1.0f : 0.5f);
                const float c1[3] = { cart ? 120.0f : 52.0f, cart ? 80.0f : 110.0f, cart ? 46.0f : 58.0f }, c2[3] = { cart ? 190.0f : 226.0f, cart ? 150.0f : 186.0f, cart ? 100.0f : 150.0f };
                for (int i = 0; i < 3; i++) { r.c1[i] = c1[i]; r.c2[i] = c2[i]; }
                gWaterRefl.push_back(r);
            }
        } else if (t.in) {   // climbing out: the ring it leaves, and the water running off
            if (gWaterWakes) AddWaterDisturb(x, z, 0.5f, size);
            if (!local) WaterRipple(x, z, surface, size / 16.0f, 0);
            WaterSpray(x, surface, z, 3);
        }
    }
    t.x = x; t.y = y; t.z = z; t.in = in; t.seen = gWaterFrame;
}

// Rain falling on the water makes the game's own rings, near the camera.
void WaterRain(PlayState* play, float dt) {
    if (!gWaterRain || !gWaterFxOn) return;
    float amount = 0.0f;
    switch (gWeatherShown.sky) {
        case royale::Sky::Rain: amount = WeatherAmount(); break;
        case royale::Sky::Thunder: amount = WeatherAmount() * 1.3f; break;
        default: break;
    }
    if (amount < 0.05f) return;
    static float carry = 0.0f;
    carry += dt * 10.0f * amount;
    const Vec3f eye = play->view.eye;
    for (; carry >= 1.0f && gWaterBudget > 0; carry -= 1.0f) {
        const float a = Flora01(gWaterFrame, 31, 601) * 6.2831853f, r = 120.0f + 900.0f * Flora01(gWaterFrame, 32, 602);
        const float x = eye.x + std::cos(a) * r, z = eye.z + std::sin(a) * r;
        float s = 0, fy = 0;
        if (WaterSurfaceAt(x, z, &s) && (!RawFloorAt(x, z, &fy) || fy < s - 8.0f)) WaterRipple(x, z, s, 0.3f + 0.2f * Flora01(gWaterFrame, 33, 603), 0);
    }
    carry = std::min(carry, 3.0f);
}

// ---- the surface -----------------------------------------------------------------------------------------------------------------------
// Vertex positions are kept in quarter units (the matrix scales them back) so a swell of a few units does not step by whole units.
void PushVtx4(Vtx& o, float x, float y, float z, float r, float g, float b, float a) {
    o.v.ob[0] = static_cast<s16>(std::lround(std::clamp(x * 4.0f, -32000.0f, 32000.0f)));
    o.v.ob[1] = static_cast<s16>(std::lround(std::clamp(y * 4.0f, -32000.0f, 32000.0f)));
    o.v.ob[2] = static_cast<s16>(std::lround(std::clamp(z * 4.0f, -32000.0f, 32000.0f)));
    o.v.flag = 0; o.v.tc[0] = o.v.tc[1] = 0;
    o.v.cn[0] = static_cast<u8>(std::clamp(r, 0.0f, 255.0f)); o.v.cn[1] = static_cast<u8>(std::clamp(g, 0.0f, 255.0f));
    o.v.cn[2] = static_cast<u8>(std::clamp(b, 0.0f, 255.0f)); o.v.cn[3] = static_cast<u8>(std::clamp(a, 0.0f, 255.0f));
}

void SetupWaterXlu(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    CLOSE_DISPS(play->state.gfxCtx);
}

// What the water sees above it: the colours of the sky at the horizon and overhead, and where the sun and the moon are (the same as DrawSky).
struct WaterSky { float hor[3], zen[3], sun[3], moon[3], sunA, moonA, sunWarm; };
WaterSky WaterSkyNow() {
    WaterSky s;
    const SkyLight L = SkyLightNow();
    float tint[3];
    const float ov = SkyOvercast(tint);
    auto mix = [](float a, float b, float k) { return a + (b - a) * k; };
    const float nz[3] = { 5, 8, 30 }, nh[3] = { 22, 28, 66 }, dz[3] = { 62, 122, 224 }, dh[3] = { 168, 206, 242 };
    const float glow[3] = { 255, 128, 62 }, glowZ[3] = { 96, 78, 150 };
    const float dim = 1.0f - 0.55f * ov * L.day;
    for (int i = 0; i < 3; i++) {
        s.zen[i] = mix(nz[i], dz[i], L.day); s.hor[i] = mix(nh[i], dh[i], L.day);
        s.zen[i] = mix(s.zen[i], glowZ[i], L.twilight * 0.55f); s.hor[i] = mix(s.hor[i], glow[i], L.twilight * 0.8f);
        const float grey = tint[i] * (0.25f + 0.75f * L.day);
        s.zen[i] = mix(s.zen[i], grey * 0.8f, ov * 0.85f) * dim; s.hor[i] = mix(s.hor[i], grey, ov * 0.85f) * dim;
    }
    const float c = std::sqrt(std::max(0.0f, 1.0f - L.sunH * L.sunH));
    s.sun[0] = std::cos(0.6f) * c; s.sun[1] = L.sunH; s.sun[2] = std::sin(0.6f) * c;
    s.moon[0] = -s.sun[0]; s.moon[1] = -s.sun[1]; s.moon[2] = -s.sun[2];
    s.sunA = std::clamp(L.sunH * 6.0f + 0.4f, 0.0f, 1.0f) * (1.0f - ov);
    s.moonA = std::clamp(-L.sunH * 6.0f + 0.4f, 0.0f, 1.0f) * (1.0f - ov);
    s.sunWarm = L.twilight;
    return s;
}

// ---- the water's textures (shared/water_sim.h): made once, on a thread of their own, the first time water is drawn. Until they are ready the
// water is drawn without them. Each is 64 x 64 8-bit intensity (the game's I8 format: the intensity is also the alpha). Caustics and glints are
// 32-frame flipbooks that loop; every frame lives at its own address, so the renderer keeps one texture per frame and never has to reload.
constexpr int kWaterTex = 64, kWaterTexFrames = 32;
alignas(16) uint8_t gWaterCausticTex[kWaterTexFrames][kWaterTex * kWaterTex];
alignas(16) uint8_t gWaterGlintTex[kWaterTexFrames][kWaterTex * kWaterTex];
alignas(16) uint8_t gWaterFoamImage[kWaterTex * kWaterTex];
std::atomic<int> gWaterTexState{ 0 };   // 0 not made, 1 being made, 2 ready

bool WaterTexturesReady() {
    int expect = 0;
    if (gWaterTexState.compare_exchange_strong(expect, 1)) {
        std::thread([] {
            royale::water::MakeCaustics(&gWaterCausticTex[0][0], kWaterTex, kWaterTexFrames);
            royale::water::MakeGlints(&gWaterGlintTex[0][0], kWaterTex, kWaterTexFrames);
            royale::water::MakeFoam(gWaterFoamImage, kWaterTex);
            gWaterTexState.store(2, std::memory_order_release);
        }).detach();
    }
    return gWaterTexState.load(std::memory_order_acquire) == 2;
}

// World units per texel of each texture. Texture coordinates are counted from an origin that moves a whole tile at a time, so they stay small
// (the renderer holds them in 16 bits) and the pattern never jumps when the grid follows the camera.
constexpr float kGlintUnits = 3.0f, kFoamUnits = 2.6f, kCausticUnits = 3.4f;
float TexOrigin(float c, float units) { const float tile = kWaterTex * units; return std::floor(c / tile) * tile; }

void PushVtxT(Vtx& o, float x, float y, float z, float s, float t, float r, float g, float b, float a) {
    PushVtx4(o, x, y, z, r, g, b, a);
    o.v.tc[0] = static_cast<s16>(std::lround(std::clamp(s * 32.0f, -32000.0f, 32000.0f)));
    o.v.tc[1] = static_cast<s16>(std::lround(std::clamp(t * 32.0f, -32000.0f, 32000.0f)));
}

// Switches the water's translucent drawing to a texture: the colour is the vertex colour, the alpha the texture times the vertex alpha. So each
// vertex says how much of the pattern shows there (and in which colour), and the pattern itself is the texture.
void WaterTexturePass(PlayState* play, const uint8_t* tex) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    gDPLoadTextureBlock(POLY_XLU_DISP++, tex, G_IM_FMT_I, G_IM_SIZ_8b, kWaterTex, kWaterTex, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, 6, 6,
                        G_TX_NOLOD, G_TX_NOLOD);
    gSPTexture(POLY_XLU_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    CLOSE_DISPS(play->state.gfxCtx);
}
void WaterPlainPass(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gSPTexture(POLY_XLU_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Draws a grid of vertices (`cols` wide) as squares, a row at a time in runs of up to 15 (two rows of 16 points fill the 32 the RSP holds),
// leaving out the squares `use` turns down.
template <typename Use>
void DrawWaterGrid(PlayState* play, const Vtx* v, int cols, int rows, Use use) {
    OPEN_DISPS(play->state.gfxCtx);
    const int V = cols + 1;
    for (int j = 0; j < rows; j++) {
        int i = 0;
        while (i < cols) {
            if (!use(i, j)) { i++; continue; }
            const int c0 = i;
            while (i < cols && i - c0 < 15 && use(i, j)) i++;
            const int m = i - c0;
            if (!GfxHasRoom(play, 4 + m)) { j = rows; break; }
            gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[j * V + c0]), m + 1, 0);
            gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[(j + 1) * V + c0]), m + 1, m + 1);
            for (int k = 0; k < m; k++) gSP2Triangles(POLY_XLU_DISP++, k, k + 1, m + 1 + k + 1, 0, k, m + 1 + k + 1, m + 1 + k, 0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// What lies under the grid points: whether there is water, its surface and its depth. The points sit on the world's own grid, so what is found
// once stays right while the camera moves (only the new row and column are looked up); it is forgotten now and then, and when the map changes.
struct WaterCell { float surface, depth; bool have; };
std::unordered_map<uint64_t, WaterCell> gWaterCells;
int gWaterCellsCell = 0, gWaterCellsAge = 0;
int gWaterCellsMap = -1;
uint64_t WaterCellKey(int gi, int gj) { return (static_cast<uint64_t>(static_cast<uint32_t>(gi + 100000)) << 32) | static_cast<uint32_t>(gj + 100000); }

// ---- caustics: the light the waves focus onto the floor under shallow water, as a sheet that lies on the floor (its own fine grid round the
// camera, 40 units a square) under the surface. Brightest in the shallows in full sun; it fades with depth, at night and under cloud.
std::unordered_map<uint64_t, WaterCell> gCausticCells;
int gCausticCellsMap = -1;

void DrawWaterCaustics(PlayState* play, float t, float light, float sunAmt, float sunWarm) {
    if (!gWaterCaustics || gWaterCausticAmt <= 0.0f || !WaterTexturesReady()) return;
    constexpr int NC = 32, VC = NC + 1;
    constexpr float cc = 40.0f;
    const Vec3f eye = play->view.eye;
    const bool island = OnIsland();
    if (gMapId != gCausticCellsMap || gCausticCells.size() > 6000) { gCausticCells.clear(); gCausticCellsMap = gMapId; }
    const int gi0 = static_cast<int>(std::lround(eye.x / cc)), gj0 = static_cast<int>(std::lround(eye.z / cc));
    const float cx = gi0 * cc, cz = gj0 * cc;
    static std::vector<WaterCell> cells;
    cells.assign(static_cast<size_t>(VC) * VC, WaterCell{ 0, 0, false });
    float baseY = 0;
    bool any = false;
    for (int j = 0; j < VC; j++)
        for (int i = 0; i < VC; i++) {
            const int gi = gi0 + i - NC / 2, gj = gj0 + j - NC / 2;
            auto it = gCausticCells.find(WaterCellKey(gi, gj));
            if (it == gCausticCells.end()) {
                WaterCell c{ 0, 0, false };
                float fy = 0;
                if (WaterSurfaceAt(gi * cc, gj * cc, &c.surface) && RawFloorAt(gi * cc, gj * cc, &fy)) {
                    c.depth = c.surface - fy;
                    if (island && !OnKingdomTerrain() && !OnRiftlandsTerrain()) c.depth = std::min(c.depth, static_cast<float>(royale::fortnite::kSeabedDrop));   // the island's sea bed is drawn no deeper than this
                    c.have = c.depth > 1.0f;
                }
                it = gCausticCells.emplace(WaterCellKey(gi, gj), c).first;
            }
            cells[j * VC + i] = it->second;
            if (it->second.have && !any) { any = true; baseY = it->second.surface; }
        }
    if (!any || eye.y - baseY > 2600.0f) return;
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(VC) * VC * sizeof(Vtx)));
    if (v == nullptr) return;
    const float amt = std::min(2.0f, gWaterCausticAmt) * (0.25f + 0.75f * std::clamp(sunAmt, 0.0f, 1.0f)) * light * light * (1.0f + 0.35f * gCamUnder);
    const float os = TexOrigin(cx, kCausticUnits), ot = TexOrigin(cz, kCausticUnits);
    float flowX, flowZ;
    WaterCurrentAt(eye.x, eye.z, t, &flowX, &flowZ);
    const float drift = std::fmod(t * 1.1f, static_cast<float>(kWaterTex));
    const float cr = 200.0f + 55.0f * sunWarm, cg = 255.0f - 30.0f * sunWarm, cb = 238.0f - 80.0f * sunWarm;   // pale aqua light, golden at dusk
    static std::vector<uint8_t> lit;
    lit.assign(static_cast<size_t>(VC) * VC, 0);
    for (int j = 0; j < VC; j++)
        for (int i = 0; i < VC; i++) {
            const WaterCell& c = cells[j * VC + i];
            const float lx = (i - NC / 2) * cc, lz = (j - NC / 2) * cc, wx = cx + lx, wz = cz + lz;
            float a = 0.0f;
            const float floorY = c.surface - c.depth;
            if (c.have) {
                const float depthK = std::clamp((c.depth - 3.0f) / 30.0f, 0.0f, 1.0f) * (1.0f - std::clamp((c.depth - 220.0f) / 420.0f, 0.0f, 1.0f));
                const float d = std::hypot(lx, lz), edge = std::clamp((NC / 2 * cc - d) / (cc * 4.0f), 0.0f, 1.0f);
                a = 215.0f * amt * depthK * edge;
                if (a > 1.0f) lit[j * VC + i] = 1;
            }
            // the pattern sways with the swell above it and drifts with the current
            const float sway = 2.2f * std::sin(t * 0.9f + wx * 0.011f) + 1.6f * std::cos(t * 0.7f + wz * 0.013f);
            PushVtxT(v[j * VC + i], lx, (c.have ? floorY : baseY - 40.0f) + 2.5f - baseY, lz, (wx - os) / kCausticUnits + drift + sway + std::fmod(flowX * 0.02f * t, static_cast<float>(kWaterTex)),
                     (wz - ot) / kCausticUnits + drift * 0.6f - sway * 0.5f + std::fmod(flowZ * 0.02f * t, static_cast<float>(kWaterTex)), cr, cg, cb, a);
        }
    const int frame = static_cast<int>(t * 11.0f) % kWaterTexFrames;
    DrawIdentity identity(&gCausticCells, gi0, gj0);
    SetupWaterXlu(play);
    WaterTexturePass(play, gWaterCausticTex[frame]);
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(cx, baseY, cz, MTXMODE_NEW);
    Matrix_Scale(0.25f, 0.25f, 0.25f, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    CLOSE_DISPS(play->state.gfxCtx);
    DrawWaterGrid(play, v, NC, NC, [&](int i, int j) {
        return lit[j * VC + i] || lit[j * VC + i + 1] || lit[(j + 1) * VC + i] || lit[(j + 1) * VC + i + 1];
    });
    WaterPlainPass(play);
}

// ---- the surface: a grid of squares that follows the camera, snapped to the world's own grid so the waves do not slide.
// The grid is coarse (a square is 140 to 360 units wide: far too wide for a ring spreading from a swimmer, which is a few dozen units across), so
// the squares that matter are drawn again as a fine grid (kFineSub x kFineSub, about 20 to 45 units): every square a swimmer, bot or cart is
// pushing, every square a ring is crossing, and the squares round the player. Only those fine squares carry the displacement and the ripple
// simulation; where a fine square borders a coarse one it runs into the coarse square's shape, so the two meet without a crack.
// The surface is drawn in up to three passes over the same points:
//   1. the water itself: its colour (turquoise shallows to deep blue, by depth), the sky mirrored in it (Fresnel), the sun's glitter path, the
//      green glow of light through the crests, foam;
//   2. the sparkling ripples: a texture of the light caught on small ripples, in the sky's and the sun's colours, stronger at a low angle;
//   3. textured foam: lacy foam on the shores, in wakes and on the crests of breaking waves.
constexpr int kFineSub = 8;      // fine squares per side of a refined coarse square
constexpr int kMaxFine = 16;     // refined squares per frame (about 1300 vertices)
constexpr float kWaterPush = 1.7f;   // how much stronger than the raw height field the displacement is drawn (it has to read from a camera behind the swimmer)

using WaterLook = royale::water::Look;

void DrawWaterSheet(PlayState* play, float t, float light) {
    namespace rw = royale::water;
    const int detail = std::clamp(gWaterDetail, 0, 2);
    const int N = detail == 0 ? 14 : detail == 1 ? 22 : 30, V = N + 1;
    const bool island = OnIsland();
    const Vec3f eye = play->view.eye;
    const float half = island ? 2550.0f : 1575.0f, cell = std::round(2.0f * half / N);
    const int icell = static_cast<int>(cell);
    if (icell != gWaterCellsCell || gMapId != gWaterCellsMap || ++gWaterCellsAge > 1200 || gWaterCells.size() > 5000) {
        gWaterCells.clear(); gWaterCellsCell = icell; gWaterCellsMap = gMapId; gWaterCellsAge = 0;
    }
    const int gi0 = static_cast<int>(std::lround(eye.x / cell)), gj0 = static_cast<int>(std::lround(eye.z / cell));
    const float cx = gi0 * cell, cz = gj0 * cell;
    // On lakes and rivers the game draws its own water just under ours, so the swell there is kept small.
    const float amp = WaveAmp() * (island ? 1.0f : 0.35f), chop = gWaterChop;
    const rw::PreparedSwell swell(t, amp, chop);
    float ampTotal = 0.0f;
    for (const rw::GerstnerWave& w : rw::kSwell) ampTotal += w.amp * amp;

    // 1. what is under each point (looked up once, then remembered)
    static std::vector<WaterCell> cells;
    cells.assign(static_cast<size_t>(V) * V, WaterCell{ 0, 0, false });
    float baseY = 0; bool any = false;
    for (int j = 0; j < V; j++) {
        for (int i = 0; i < V; i++) {
            const int gi = gi0 + i - N / 2, gj = gj0 + j - N / 2;
            auto it = gWaterCells.find(WaterCellKey(gi, gj));
            if (it == gWaterCells.end()) {
                WaterCell c{ 0, 0, false };
                const float wx = gi * cell, wz = gj * cell;
                if (WaterSurfaceAt(wx, wz, &c.surface)) {
                    float fy = 0;
                    c.have = true;
                    c.depth = RawFloorAt(wx, wz, &fy) ? c.surface - fy : 1000.0f;
                }
                it = gWaterCells.emplace(WaterCellKey(gi, gj), c).first;
            }
            cells[j * V + i] = it->second;
            if (it->second.have && !any) { any = true; baseY = it->second.surface; }
        }
    }
    if (!any) return;

    const WaterSky sky = WaterSkyNow();
    // caustics first: they lie on the floor, under the surface
    DrawWaterCaustics(play, t, light, std::max(sky.sunA, 0.3f * sky.moonA), sky.sunWarm);

    // 2. the surface's look at a point: `y` its height, n its normal, `foamWake` the foam the swimmers left, `fold` how near the crest is to
    //    breaking, `crest` its height above calm water.
    float wx0, wz0, windNow;
    WindNow(&wx0, &wz0, &windNow);
    const bool texOk = WaterTexturesReady();
    const float glintAmt = gWaterGlints && texOk ? std::min(2.0f, gWaterGlintAmt) : 0.0f;
    const float foamAmt = gWaterFoamTex && texOk ? std::min(2.0f, gWaterFoamAmt) : 0.0f;
    const float reach = 60.0f + 320.0f * gWaterClarity;   // how deep the water must be to hide the floor
    royale::water::LookParams look;
    look.eye[0] = eye.x; look.eye[1] = eye.y; look.eye[2] = eye.z;
    look.t = t; look.light = light; look.reach = reach; look.ampTotal = ampTotal; look.wind = windNow;
    look.glintAmt = glintAmt; look.foamAmt = foamAmt; look.skyOn = gWaterSkyOn;
    royale::water::LookSky lsky;
    for (int q = 0; q < 3; q++) { lsky.hor[q] = sky.hor[q]; lsky.zen[q] = sky.zen[q]; lsky.sun[q] = sky.sun[q]; lsky.moon[q] = sky.moon[q]; }
    lsky.sunA = sky.sunA; lsky.moonA = sky.moonA; lsky.sunWarm = sky.sunWarm;
    auto shade = [&](float wx, float wz, float y, float depth, float nx, float ny, float nz, float foamWake, float fold, float crest, int hi, int hj, float fade,
                     WaterLook& o) {
        royale::water::ShadeWater(look, lsky, wx, wz, y, depth, nx, ny, nz, foamWake, fold, crest, Flora01(hi, hj, 611), fade, o);
    };
    // where the surface sits: over the game's own water on lakes and rivers, on the island's (lowered) sea bed there is room to dip
    const float baseLift = island ? 0.0f : 5.0f, lowest = island ? -34.0f : 1.0f;
    const float og = TexOrigin(cx, kGlintUnits), ogz = TexOrigin(cz, kGlintUnits), of = TexOrigin(cx, kFoamUnits), ofz = TexOrigin(cz, kFoamUnits);
    float flowX, flowZ;
    WaterCurrentAt(eye.x, eye.z, t, &flowX, &flowZ);
    const float gS = std::fmod(t * 1.7f, static_cast<float>(kWaterTex)), gT = std::fmod(t * 1.1f, static_cast<float>(kWaterTex));
    const float fS = std::fmod(flowX * t / kFoamUnits, static_cast<float>(kWaterTex)), fT = std::fmod(flowZ * t / kFoamUnits, static_cast<float>(kWaterTex));
    // Writes a point of each pass: (px, py, pz) relative to the grid's origin, (wx, wz) where on the water it is, n its normal (bends the sparkle)
    auto emit = [&](Vtx* base, Vtx* gl, Vtx* fo, size_t at, float px, float py, float pz, float wx, float wz, float nx, float nz, const WaterLook& o) {
        PushVtx4(base[at], px, py, pz, o.col[0], o.col[1], o.col[2], o.col[3]);
        if (gl != nullptr)
            PushVtxT(gl[at], px, py + 0.5f, pz, (wx - og) / kGlintUnits + gS + nx * 9.0f, (wz - ogz) / kGlintUnits + gT + nz * 9.0f, o.glint[0], o.glint[1], o.glint[2], o.glint[3]);
        if (fo != nullptr) {
            const float fl = 0.6f + 0.4f * light;
            PushVtxT(fo[at], px, py + 0.7f, pz, (wx - of) / kFoamUnits + fS + nx * 4.0f, (wz - ofz) / kFoamUnits + fT + nz * 4.0f, 240.0f * fl, 248.0f * fl, 252.0f * fl, o.foam);
        }
    };

    const size_t nv = static_cast<size_t>(V) * V;
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, nv * sizeof(Vtx)));
    if (v == nullptr) return;
    Vtx* vg = glintAmt > 0.0f ? static_cast<Vtx*>(FrameAlloc(play, nv * sizeof(Vtx))) : nullptr;
    Vtx* vf = foamAmt > 0.0f ? static_cast<Vtx*>(FrameAlloc(play, nv * sizeof(Vtx))) : nullptr;
    static std::vector<uint8_t> clear;   // a point where there is nothing to show
    static std::vector<float> cSW, cOX, cOZ;   // each coarse point's swell (height and sideways shift), for the fine squares to run into
    static std::vector<uint8_t> hasGlint, hasFoam;
    clear.assign(nv, 1); cSW.assign(nv, 0.0f); cOX.assign(nv, 0.0f); cOZ.assign(nv, 0.0f); hasGlint.assign(nv, 0); hasFoam.assign(nv, 0);
    float lastSurface = baseY;
    for (int j = 0; j < V; j++) {
        for (int i = 0; i < V; i++) {
            const size_t id = static_cast<size_t>(j) * V + i;
            const WaterCell& c = cells[id];
            const float lx = (i - N / 2) * cell, lz = (j - N / 2) * cell, wx = cx + lx, wz = cz + lz;
            WaterLook o{};
            float y = lastSurface, px = lx, pz = lz, nx = 0.0f, nz = 0.0f;
            if (c.have) {
                lastSurface = c.surface;
                y = c.surface;
                if (c.depth > 2.0f) {
                    const float d = std::hypot(wx - eye.x, wz - eye.z), fade = std::clamp((half - d) / (half * 0.35f), 0.0f, 1.0f);
                    if (fade > 0.0f) {
                        clear[id] = 0;
                        const float shore = std::min(1.0f, c.depth / 90.0f);
                        const rw::SwellPoint sp = swell.Sample(wx, wz);
                        cSW[id] = sp.h * shore; cOX[id] = sp.ox * shore; cOZ[id] = sp.oz * shore;
                        y += royale::graphics::WaterSurfaceOffset(cSW[id], 0.0f, 1.0f, baseLift, lowest);
                        px += cOX[id]; pz += cOZ[id];
                        float ny = sp.ny;
                        nx = sp.nx * shore; nz = sp.nz * shore;
                        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
                        shade(wx, wz, y, c.depth, nx, ny, nz, 0.0f, sp.fold * shore, cSW[id], i + gi0, j + gj0, fade, o);
                        hasGlint[id] = o.glint[3] > 1.0f; hasFoam[id] = o.foam > 1.0f;
                    }
                }
            }
            emit(v, vg, vf, id, px, y - baseY, pz, wx, wz, nx, nz, o);
        }
    }

    // 3. which squares are drawn again as a fine grid: the ones a body or a ring touches first, then the ones round the player
    static std::vector<uint8_t> refined;
    refined.assign(static_cast<size_t>(N) * N, 0);
    auto quadOk = [&](int i, int j) {
        return i >= 0 && j >= 0 && i < N && j < N && !clear[j * V + i] && !clear[j * V + i + 1] && !clear[(j + 1) * V + i] && !clear[(j + 1) * V + i + 1] &&
               cells[j * V + i].have && cells[j * V + i + 1].have && cells[(j + 1) * V + i].have && cells[(j + 1) * V + i + 1].have &&
               std::fabs(cells[j * V + i].surface - cells[j * V + i + 1].surface) < 1.0f &&
               std::fabs(cells[j * V + i].surface - cells[(j + 1) * V + i].surface) < 1.0f &&
               std::fabs(cells[j * V + i].surface - cells[(j + 1) * V + i + 1].surface) < 1.0f;
    };
    struct FineQuad { int i, j; };
    static std::vector<FineQuad> fine;
    fine.clear();
    {
        struct Cand { float pri; int i, j; };
        static std::vector<Cand> cand;
        cand.clear();
        Player* pl = GET_PLAYER(play);
        const float ppx = pl != nullptr ? pl->actor.world.pos.x : eye.x, ppz = pl != nullptr ? pl->actor.world.pos.z : eye.z;
        const bool anyPush = !gWaterBodies.empty() || !gWaterDist.empty();
        for (int j = 0; j < N; j++)
            for (int i = 0; i < N; i++) {
                if (!quadOk(i, j)) continue;
                const float qx = cx + (i + 0.5f - N / 2) * cell, qz = cz + (j + 0.5f - N / 2) * cell, slack = 0.75f * cell;
                bool touched = false;
                if (anyPush && gWaterWakes) {
                    for (const WaterBodyNow& b : gWaterBodies)
                        if (std::hypot(qx - b.x, qz - b.z) < b.size * 6.4f + slack) { touched = true; break; }
                    for (size_t di = 0; di < gWaterDist.size() && !touched; di++) {
                        const WaterDisturb& d = gWaterDist[di];
                        const float a = gWaterNow - d.birth, reach = kWaterRingSpeed * a + (26.0f + d.size * 1.3f) * 2.5f + d.size * 2.0f;
                        if (std::hypot(qx - d.x, qz - d.z) < reach + slack) touched = true;
                    }
                }
                const float dp = std::hypot(qx - ppx, qz - ppz), de = std::hypot(qx - eye.x, qz - eye.z);
                if (touched) cand.push_back({ de * 0.001f, i, j });
                else if (dp < 1.7f * cell) cand.push_back({ 100.0f + dp * 0.001f, i, j });
            }
        std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.pri < b.pri; });
        for (size_t k = 0; k < cand.size() && fine.size() < static_cast<size_t>(kMaxFine); k++) { fine.push_back({ cand[k].i, cand[k].j }); refined[cand[k].j * N + cand[k].i] = 1; }
    }
    constexpr int S = kFineSub, FV = S + 1;
    const size_t nf = fine.size() * FV * FV;
    Vtx* fv = fine.empty() ? nullptr : static_cast<Vtx*>(FrameAlloc(play, nf * sizeof(Vtx)));
    Vtx* fg = fv != nullptr && vg != nullptr ? static_cast<Vtx*>(FrameAlloc(play, nf * sizeof(Vtx))) : nullptr;
    Vtx* ff = fv != nullptr && vf != nullptr ? static_cast<Vtx*>(FrameAlloc(play, nf * sizeof(Vtx))) : nullptr;
    if (fv == nullptr || (vg != nullptr && fg == nullptr) || (vf != nullptr && ff == nullptr)) { fine.clear(); std::fill(refined.begin(), refined.end(), 0); }
    auto isRef = [&](int i, int j) { return i >= 0 && j >= 0 && i < N && j < N && refined[j * N + i] != 0; };
    auto ss01 = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
    for (size_t q = 0; q < fine.size(); q++) {
        const int qi = fine[q].i, qj = fine[q].j;
        const int k00 = qj * V + qi, k10 = k00 + 1, k01 = k00 + V, k11 = k01 + 1;
        const WaterCell& c00 = cells[k00], &c10 = cells[k10], &c01 = cells[k01], &c11 = cells[k11];
        // the neighbours that are not fine: the fine shape runs into the coarse one toward them
        const bool eL = !isRef(qi - 1, qj), eR = !isRef(qi + 1, qj), eT = !isRef(qi, qj - 1), eB = !isRef(qi, qj + 1);
        const bool cTL = !isRef(qi - 1, qj - 1), cTR = !isRef(qi + 1, qj - 1), cBL = !isRef(qi - 1, qj + 1), cBR = !isRef(qi + 1, qj + 1);
        const float step = cell / S;
        float H[FV * FV], P[FV * FV], FW[FV * FV], DP[FV * FV], SF[FV * FV], WX[FV * FV], WZ[FV * FV], PX[FV * FV], PZ[FV * FV], NX[FV * FV], NY[FV * FV], NZ[FV * FV], FO[FV * FV], CR[FV * FV];
        auto bil = [](float a, float b, float c, float d, float u, float w) { return (a * (1 - u) + b * u) * (1 - w) + (c * (1 - u) + d * u) * w; };
        for (int b = 0; b <= S; b++)
            for (int a = 0; a <= S; a++) {
                const int id = b * FV + a;
                const float u = static_cast<float>(a) / S, w = static_cast<float>(b) / S;
                const float lx = (qi - N / 2 + u) * cell, lz = (qj - N / 2 + w) * cell, wx = cx + lx, wz = cz + lz;
                WX[id] = wx; WZ[id] = wz;
                SF[id] = bil(c00.surface, c10.surface, c01.surface, c11.surface, u, w);
                DP[id] = bil(c00.depth, c10.depth, c01.depth, c11.depth, u, w);
                float wgt = 1.0f;
                if (eL) wgt *= ss01(u * S * 0.5f);
                if (eR) wgt *= ss01((1 - u) * S * 0.5f);
                if (eT) wgt *= ss01(w * S * 0.5f);
                if (eB) wgt *= ss01((1 - w) * S * 0.5f);
                if (cTL) wgt *= ss01(std::hypot(u, w) * S * 0.5f);
                if (cTR) wgt *= ss01(std::hypot(1 - u, w) * S * 0.5f);
                if (cBL) wgt *= ss01(std::hypot(u, 1 - w) * S * 0.5f);
                if (cBR) wgt *= ss01(std::hypot(1 - u, 1 - w) * S * 0.5f);
                const float shore = std::min(1.0f, DP[id] / 90.0f);
                const rw::SwellPoint sp = swell.Sample(wx, wz);
                // the exact swell, run into the coarse square's own (straight-line) shape toward coarse neighbours
                const float iSW = bil(cSW[k00], cSW[k10], cSW[k01], cSW[k11], u, w), iOX = bil(cOX[k00], cOX[k10], cOX[k01], cOX[k11], u, w),
                            iOZ = bil(cOZ[k00], cOZ[k10], cOZ[k01], cOZ[k11], u, w);
                const float sw = iSW + (sp.h * shore - iSW) * wgt;
                PX[id] = lx + iOX + (sp.ox * shore - iOX) * wgt;
                PZ[id] = lz + iOZ + (sp.oz * shore - iOZ) * wgt;
                float foam = 0.0f;
                float push = gWaterWakes || gWaterRipples ? WaterDisplaceAt(wx, wz, &foam) : 0.0f;
                push *= kWaterPush * wgt;
                FW[id] = foam * wgt;
                P[id] = push;
                H[id] = royale::graphics::WaterSurfaceOffset(sw, push, shore, baseLift, lowest);
                NX[id] = sp.nx * shore; NY[id] = sp.ny; NZ[id] = sp.nz * shore;
                FO[id] = sp.fold * shore;
                CR[id] = sw;
            }
        for (int b = 0; b <= S; b++)
            for (int a = 0; a <= S; a++) {
                const int id = b * FV + a;
                const int a0 = std::max(0, a - 1), a1 = std::min(S, a + 1), b0 = std::max(0, b - 1), b1 = std::min(S, b + 1);
                // the swell's own normal, tipped by the slope of the displacement and the ripples
                const float sx = (P[b * FV + a1] - P[b * FV + a0]) / ((a1 - a0) * step), sz = (P[b1 * FV + a] - P[b0 * FV + a]) / ((b1 - b0) * step);
                float nx = NX[id] - sx * NY[id], ny = NY[id], nz = NZ[id] - sz * NY[id];
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
                const float d = std::hypot(WX[id] - eye.x, WZ[id] - eye.z), fade = std::clamp((half - d) / (half * 0.35f), 0.0f, 1.0f);
                WaterLook o{};
                const float y = SF[id] + H[id];
                shade(WX[id], WZ[id], y, DP[id], nx, ny, nz, FW[id], FO[id], CR[id], static_cast<int>(WX[id] / step), static_cast<int>(WZ[id] / step), fade, o);
                emit(fv, fg, ff, q * FV * FV + id, PX[id] - 0.0f, y - baseY, PZ[id], WX[id], WZ[id], nx, nz, o);
            }
    }

    // The vertex grid is rebuilt at its new origin immediately. Interpolating the
    // old origin with these new vertices would move the entire sheet between cells.
    DrawIdentity identity(&gWaterCells, gi0, gj0);
    SetupWaterXlu(play);
    {
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Translate(cx, baseY, cz, MTXMODE_NEW);
        Matrix_Scale(0.25f, 0.25f, 0.25f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    // the fine squares: three rows of points (27 of the 32 the RSP holds) cover two rows of squares
    auto drawFine = [&](const Vtx* arr) {
        OPEN_DISPS(play->state.gfxCtx);
        for (size_t q = 0; q < fine.size(); q++) {
            const Vtx* base = &arr[q * FV * FV];
            for (int r = 0; r < S; r += 2) {
                const int rows = std::min(3, S - r + 1);
                if (!GfxHasRoom(play, 4 + 2 * S)) break;
                gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&base[r * FV]), rows * FV, 0);
                for (int rr = 0; rr + 1 < rows; rr++)
                    for (int a = 0; a < S; a++) {
                        const int i0 = rr * FV + a, i1 = i0 + 1, i2 = i0 + FV + 1, i3 = i0 + FV;
                        gSP2Triangles(POLY_XLU_DISP++, i0, i1, i2, 0, i0, i2, i3, 0);
                    }
            }
        }
        CLOSE_DISPS(play->state.gfxCtx);
    };
    auto any4 = [&](const std::vector<uint8_t>& f, int i, int j) { return f[j * V + i] || f[j * V + i + 1] || f[(j + 1) * V + i] || f[(j + 1) * V + i + 1]; };
    // pass 1: the water (leaving out the empty squares and the fine ones)
    DrawWaterGrid(play, v, N, N, [&](int i, int j) {
        return !refined[j * N + i] && !(clear[j * V + i] && clear[j * V + i + 1] && clear[(j + 1) * V + i] && clear[(j + 1) * V + i + 1]);
    });
    if (fv != nullptr) drawFine(fv);
    // pass 2: the sparkling ripples (the texture is only drawn fairly near: far off it would shimmer)
    const float texReach = half * 0.62f;
    auto nearQ = [&](int i, int j) { return std::hypot((i + 0.5f - N / 2) * cell + cx - eye.x, (j + 0.5f - N / 2) * cell + cz - eye.z) < texReach; };
    if (vg != nullptr) {
        WaterTexturePass(play, gWaterGlintTex[static_cast<int>(t * 9.0f) % kWaterTexFrames]);
        DrawWaterGrid(play, vg, N, N, [&](int i, int j) { return !refined[j * N + i] && any4(hasGlint, i, j) && nearQ(i, j); });
        if (fg != nullptr) drawFine(fg);
    }
    // pass 3: the foam
    if (vf != nullptr) {
        WaterTexturePass(play, gWaterFoamImage);
        DrawWaterGrid(play, vf, N, N, [&](int i, int j) { return !refined[j * N + i] && any4(hasFoam, i, j) && nearQ(i, j); });
        if (ff != nullptr) drawFine(ff);
    }
    if (vg != nullptr || vf != nullptr) WaterPlainPass(play);
    OPEN_DISPS(play->state.gfxCtx);
    // the reflections of whoever stands or swims here: a strip from the feet toward the camera, in their colours, wobbling with the swell
    if (!gWaterRefl.empty() && gWaterSkyOn && gWaterBodiesRefl) {
        constexpr int S = 4;   // slices
        Vtx* rv = static_cast<Vtx*>(FrameAlloc(play, gWaterRefl.size() * 2 * S * sizeof(Vtx)));
        if (rv != nullptr) {
            Vtx* o = rv;
            for (const WaterRefl& w : gWaterRefl) {
                const float height = eye.y - w.surface;
                if (height < 5.0f) continue;
                const float tt = height / (height + w.h);   // where the line to the mirrored head meets the surface
                const float fx = w.x - eye.x, fz = w.z - eye.z, fl = std::max(1.0f, std::hypot(fx, fz)), pxn = -fz / fl, pzn = fx / fl;
                const float cosv = std::clamp(height / std::sqrt(height * height + fl * fl), 0.0f, 1.0f);
                const float fres = 0.28f + 0.6f * std::pow(1.0f - cosv, 2.0f);
                for (int s = 0; s < S; s++) {
                    const float u = static_cast<float>(s) / (S - 1);
                    const float px = w.x - fx * (1.0f - tt) * u, pz = w.z - fz * (1.0f - tt) * u;   // from the feet (u 0) to the mirrored head (u 1)
                    const float wob = std::sin(t * 4.0f + u * 3.0f + w.x * 0.02f) * w.w * 0.2f * (0.5f + amp);
                    const float wd = w.w * (1.0f - 0.3f * u);
                    float col[3];
                    for (int q = 0; q < 3; q++) col[q] = (w.c1[q] + (w.c2[q] - w.c1[q]) * u) * (0.45f + 0.55f * light);
                    const float al = 150.0f * fres * (1.0f - 0.75f * u) * std::min(1.0f, (eye.y - w.surface) / 60.0f);
                    const float oy = w.surface + 3.5f - baseY;
                    PushVtx4(o[s * 2], px - cx + pxn * (wob - wd), oy, pz - cz + pzn * (wob - wd), col[0], col[1], col[2], al * (s == 0 ? 0.6f : 1.0f));
                    PushVtx4(o[s * 2 + 1], px - cx + pxn * (wob + wd), oy, pz - cz + pzn * (wob + wd), col[0], col[1], col[2], al * (s == 0 ? 0.6f : 1.0f));
                }
                gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(o), 2 * S, 0);
                for (int s = 0; s + 1 < S; s++) gSP2Triangles(POLY_XLU_DISP++, s * 2, s * 2 + 1, s * 2 + 3, 0, s * 2, s * 2 + 3, s * 2 + 2, 0);
                o += 2 * S;
            }
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Puddles get the water's look too: the sky mirrored in them and the sparkling ripples (the same texture as the open water), as a soft oval over
// each puddle that fades out toward its rim. Drawn after all the ground patches, as a decal on them.
void DrawPuddleSheens(PlayState* play) {
    if (gPuddleSheens.empty() || !DebugOn(kDbgWater) || !gWaterGlints || gWaterGlintAmt <= 0.0f || !WaterTexturesReady()) return;
    constexpr int R = 12;   // rim points
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, gPuddleSheens.size() * (R + 1) * sizeof(Vtx)));
    if (v == nullptr) return;
    const float t = static_cast<float>(ImGui::GetTime()), light = WaterLight();
    const WaterSky sky = WaterSkyNow();
    const float amt = std::min(2.0f, gWaterGlintAmt);
    float col[3];
    for (int q = 0; q < 3; q++) col[q] = std::min(255.0f, (sky.hor[q] + sky.zen[q]) * 0.6f + 80.0f) * (0.55f + 0.45f * light);
    const float gS = std::fmod(t * 1.2f, static_cast<float>(kWaterTex)), gT = std::fmod(t * 0.8f, static_cast<float>(kWaterTex));
    SetupWaterXlu(play);
    WaterTexturePass(play, gWaterGlintTex[static_cast<int>(t * 9.0f) % kWaterTexFrames]);
    OPEN_DISPS(play->state.gfxCtx);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2);   // nudged toward the camera, like the puddle under it
    for (size_t k = 0; k < gPuddleSheens.size(); k++) {
        const PuddleSheen& p = gPuddleSheens[k];
        if (!GfxHasRoom(play, 12)) break;
        Vtx* o = &v[k * (R + 1)];
        const float ys = std::sin(p.yaw), yc = std::cos(p.yaw), os = TexOrigin(p.x, kGlintUnits), ot = TexOrigin(p.z, kGlintUnits);
        const float centreA = amt * p.fade * (90.0f + 70.0f * light);
        for (int r = 0; r <= R; r++) {
            float lx = 0.0f, lz = 0.0f, al = centreA;
            if (r > 0) {
                const float ang = (r - 1) * 6.2831853f / R;
                lx = std::cos(ang) * p.a * 0.92f; lz = std::sin(ang) * p.b * 0.92f;
                al = 0.0f;
            }
            const float ox = lx * yc + lz * ys, oz = -lx * ys + lz * yc;
            const float wx = p.x + ox, wz = p.z + oz;
            PushVtxT(o[r], ox, p.sx * ox + p.sz * oz, oz, (wx - os) / kGlintUnits + gS, (wz - ot) / kGlintUnits + gT, col[0], col[1], col[2], al);
        }
        Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
        Matrix_Scale(0.25f, 0.25f, 0.25f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(o), R + 1, 0);
        for (int r = 1; r <= R; r += 2) {
            const int a1 = r, a2 = r % R + 1, a3 = a2 % R + 1;
            gSP2Triangles(POLY_XLU_DISP++, 0, a1, a2, 0, 0, a2, a3, 0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
    WaterPlainPass(play);
    gPuddleSheens.clear();
}

// Drawn every frame with the other world effects (Projectile_Draw).
void CollectCartWalkers(const std::function<void(const void*, float, float, float, float)>& add) {
    for (const auto& [index, c] : gCarts)
        if (c.actor != nullptr && !c.wrecked) add(&c, c.body.x, c.body.y, c.body.z, 46.0f);
}

void DrawWater(PlayState* play) {
    if (!DebugOn(kDbgWater) || !InField() || gPlayState == nullptr) { gWaterDist.clear(); gWaterTrack.clear(); gWaterCells.clear(); gCausticCells.clear(); if (!gRipples.Sleeping()) gRipples.Clear(); gCamUnder = 0.0f; return; }
    static int previousMap = -1;
    if (previousMap != gMapId) {
        gWaterDist.clear(); gWaterTrack.clear(); gWaterCells.clear(); gCausticCells.clear();
        gRipples.Clear(); gCamUnder = 0.0f; previousMap = gMapId;
    }
    Feat("draw: water");
    const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime), t = static_cast<float>(ImGui::GetTime()), light = WaterLight();
    gWaterFrame++;
    gWaterNow = t;
    const bool paused = play->pauseCtx.state != 0;
    gWaterBudget = paused ? 0 : 6;   // effects the game's own effect table takes this frame
    gBubbleBudget = paused ? 0 : 9;   // and bubbles and spray of ours
    gBubbleLoad = std::max(0.0f, gBubbleLoad - dt * 14.0f);
    for (size_t i = 0; i < gWaterDist.size();) { if (t - gWaterDist[i].birth > kWaterRingLife) gWaterDist.erase(gWaterDist.begin() + i); else i++; }
    gWaterBodies.clear();
    gWaterRefl.clear();
    Player* player = GET_PLAYER(play);
    if (player == nullptr) return;
    const float pdt = paused ? 0.0f : dt;
    WaterBody(player, player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z, 16.0f, pdt, true);
    for (const auto& [id, actor] : gActorOf) {
        if (actor == nullptr || actor == &player->actor) continue;
        WaterBody(actor, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, 16.0f * actor->scale.y / 0.01f, pdt, false);
    }
    for (const auto& [index, c] : gCarts) {
        if (c.actor == nullptr || c.wrecked) continue;
        WaterBody(&c, c.body.x, c.body.y, c.body.z, 46.0f, pdt, false);
    }
    for (auto it = gWaterTrack.begin(); it != gWaterTrack.end();) it = it->second.seen != gWaterFrame ? gWaterTrack.erase(it) : std::next(it);
    // The ripple simulation follows the player. Whoever moves through it keeps pushing it (a wake fans out behind, rings cross and mix);
    // someone treading water bobs and sends out slow rings; rain pocks it.
    if (gWaterRipples) {
        gRipples.Recenter(player->actor.world.pos.x, player->actor.world.pos.z);
        const float k = pdt * 60.0f;
        for (const WaterBodyNow& b : gWaterBodies) {
            if (!gRipples.Contains(b.x, b.z, gRipples.Cell() * 4.0f)) continue;
            const float run = std::min(1.5f, b.speed / 120.0f);
            if (run > 0.05f) gRipples.Impulse(b.x, b.z, b.size * 0.05f * run * k, b.size * 0.85f);
            else gRipples.Impulse(b.x, b.z, b.size * 0.03f * std::sin(t * 3.1f + b.x * 0.01f) * k, b.size * 0.8f);
        }
        if ((gWeatherShown.sky == royale::Sky::Rain || gWeatherShown.sky == royale::Sky::Thunder) && gWaterRain && pdt > 0.0f) {
            const int drops = static_cast<int>(WeatherAmount() * 6.0f * k + Flora01(gWaterFrame, 41, 690));
            for (int d = 0; d < drops; d++) {
                const float x = gRipples.MinX() + gRipples.Size() * Flora01(gWaterFrame, d, 691);
                const float z = gRipples.MinZ() + gRipples.Size() * Flora01(gWaterFrame, d, 692);
                float surface = 0;
                if (WaterSurfaceAt(x, z, &surface)) gRipples.Impulse(x, z, 1.2f + 1.5f * Flora01(gWaterFrame, d, 693), 10.0f);
            }
        }
        gRipples.Step(pdt);
    } else {
        if (!gRipples.Sleeping()) gRipples.Clear();
    }
    WaterRain(play, pdt);
    WaterLife(play, pdt, paused);
    // Is the camera under the surface?
    float surface = 0, floorY = 0;
    const Vec3f eye = play->view.eye;
    const bool under = gWaterUnder && WaterSurfaceAt(eye.x, eye.z, &surface) && eye.y < surface && (!RawFloorAt(eye.x, eye.z, &floorY) || floorY < surface - 6.0f);
    gCamUnder += ((under ? 1.0f : 0.0f) - gCamUnder) * std::min(1.0f, dt * 8.0f);
    if (under) gCamDepth = surface - eye.y;
    DrawWaterSheet(play, t, light);
}

// The look from below the surface (the Zora's-Domain-under-water look): the screen goes teal, bright toward the surface and deep blue below,
// darker the deeper you are; soft shafts of light lean down from above and sway; the surface shimmers overhead when you are near it; motes
// drift in the water and bubbles wobble up (white rings with a glint, as the game draws its own).
void DrawUnderwaterOverlay(ImDrawList* dl, ImVec2 ds) {
    if (!DebugOn(kDbgWater) || gCamUnder < 0.02f || gWaterUnderAmt <= 0.0f) return;
    const float k = gCamUnder * std::min(2.0f, gWaterUnderAmt), t = static_cast<float>(ImGui::GetTime()), light = WaterLight();
    const float deep = std::clamp(gCamDepth / 650.0f, 0.0f, 1.0f), px = ds.y / 720.0f;
    const auto a = [&](float v) { return static_cast<int>(std::clamp(v * k, 0.0f, 255.0f)); };
    const auto col = [&](float r, float g, float b, float al) { return IM_COL32(static_cast<int>(std::clamp(r, 0.0f, 255.0f)), static_cast<int>(std::clamp(g, 0.0f, 255.0f)), static_cast<int>(std::clamp(b, 0.0f, 255.0f)), a(al)); };
    const float lt = light * (1.0f - 0.45f * deep);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, col(70 * lt, 200 * lt, 212 * lt, 70 + 50 * deep), col(70 * lt, 200 * lt, 212 * lt, 70 + 50 * deep),
                                col(6, 58 * lt, 104 * lt, 140 + 60 * deep), col(6, 58 * lt, 104 * lt, 140 + 60 * deep));
    // light shafts: each a wide faint quad with a narrower, brighter core
    const float shaftA = 20.0f * light * (1.0f - 0.75f * deep);
    for (int i = 0; i < 8; i++) {
        const float x = ds.x * (0.04f + 0.125f * i + 0.04f * std::sin(t * 0.3f + i * 1.9f)), lean = ds.y * (0.22f + 0.06f * std::sin(t * 0.21f + i));
        const float len = ds.y * (0.75f + 0.25f * std::sin(i * 2.3f)), pulse = 0.6f + 0.4f * std::sin(t * 0.8f + i * 2.7f);
        for (int layer = 0; layer < 2; layer++) {
            const float w = ds.x * (layer == 0 ? 0.06f : 0.022f) * (0.7f + 0.3f * std::sin(t * 0.5f + i));
            const float al = shaftA * pulse * (layer == 0 ? 0.7f : 1.3f);
            dl->AddQuadFilled(ImVec2(x - w * 0.5f, 0), ImVec2(x + w * 0.5f, 0), ImVec2(x + w * 0.9f + lean, len), ImVec2(x - w * 0.1f + lean * 0.9f, len),
                              col(200, 255, 245, al));
        }
    }
    // the surface overhead, rippling, when it is close
    const float nearTop = std::clamp(1.0f - gCamDepth / 260.0f, 0.0f, 1.0f);
    if (nearTop > 0.02f)
        for (int line = 0; line < 4; line++) {
            const float y0 = ds.y * (0.015f + 0.03f * line);
            ImVec2 prev(0, y0);
            for (int sgm = 1; sgm <= 24; sgm++) {
                const float x = ds.x * sgm / 24.0f, y = y0 + std::sin(x * 0.012f / px + t * (1.3f + 0.4f * line) + line * 2.0f) * 5.0f * px;
                dl->AddLine(prev, ImVec2(x, y), col(220, 255, 250, 60.0f * nearTop * light * (1.0f - line * 0.2f)), 2.0f * px);
                prev = ImVec2(x, y);
            }
        }
    // motes drifting in the water
    for (int i = 0; i < 40; i++) {
        const float sx = royale::water::Hash01(i, 1, 77), sy = royale::water::Hash01(i, 2, 77), sp = 0.01f + 0.02f * royale::water::Hash01(i, 3, 77);
        const float x = ds.x * std::fmod(sx + 0.02f * std::sin(t * 0.4f + i), 1.0f), y = ds.y * (1.0f - std::fmod(sy + t * sp, 1.0f));
        dl->AddCircleFilled(ImVec2(x, y), (1.0f + 1.5f * royale::water::Hash01(i, 4, 77)) * px, col(210, 255, 240, 70.0f * light), 6);
    }
    // bubbles wobbling up
    for (int i = 0; i < 9; i++) {
        const float life = std::fmod(t * (0.12f + 0.05f * royale::water::Hash01(i, 5, 78)) + royale::water::Hash01(i, 6, 78), 1.0f);
        const float x = ds.x * (0.08f + 0.84f * royale::water::Hash01(i, 7, 78)) + std::sin(t * 3.0f + i) * 6.0f * px, y = ds.y * (1.05f - 1.1f * life);
        const float r = (4.0f + 7.0f * royale::water::Hash01(i, 8, 78)) * px;
        dl->AddCircle(ImVec2(x, y), r, col(235, 255, 255, 120), 12, 1.6f * px);
        dl->AddCircleFilled(ImVec2(x - r * 0.35f, y - r * 0.35f), r * 0.25f, col(255, 255, 255, 150), 6);
    }
}

// Swim physics, once per game update: in the water a current carries you along (so treading water drifts and swimming against it is slower),
// and you bob with the swell. Only the local player's own body; the server and the others are unaffected.
void ApplySwimPhysics(Player* player) {
    if (!DebugOn(kDbgWater) || !gWaterCurrent || !InField() || player == nullptr) return;
    Feat("swim physics");
    if (!(player->stateFlags1 & PLAYER_STATE1_IN_WATER)) return;
    const float t = static_cast<float>(ImGui::GetTime()), dt = 1.0f / royale::kTickHz;
    float cx, cz, surface = 0;
    WaterCurrentAt(player->actor.world.pos.x, player->actor.world.pos.z, t, &cx, &cz);
    const float nx = player->actor.world.pos.x + cx * dt, nz = player->actor.world.pos.z + cz * dt;
    float floorY = 0;
    if (WaterSurfaceAt(nx, nz, &surface) && RawFloorAt(nx, nz, &floorY) && floorY < surface - 40.0f) {   // never drift a swimmer into the shore
        player->actor.world.pos.x = nx;
        player->actor.world.pos.z = nz;
    }
    player->actor.velocity.y += std::sin(t * 2.4f) * 0.04f * WaveAmp();
}


