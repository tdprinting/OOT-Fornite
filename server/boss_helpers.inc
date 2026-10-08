// Included inside Match. A helper kind is separate from bosses, players and hired allies.
const std::vector<BossHelper>& Helpers() const { return helpers; }
const BossHelper* FindHelper(uint32_t id) const {
    for (const auto& h : helpers) if (h.id == id) return &h;
    return nullptr;
}
void HelperMode(BossHelper& h, BokoMode mode, bool restart = false) {
    if (h.mode == mode && !restart) return;
    h.mode = mode; h.modeAt = clock; h.action++; h.resolved = false;
    h.y = 0;
}
void SpawnHelpers(const MiniBoss& boss) {
    if (!boss.alive || IsDragonKind(boss.kind) || !IsBossId(boss.id)) return;
    helpers.erase(std::remove_if(helpers.begin(),helpers.end(),[&](const BossHelper& h) { return h.boss==boss.id; }),helpers.end());
    Rng rng(seed ^ (static_cast<uint64_t>(boss.id)<<16) ^ 0x626F6B6Full);
    for (int i=0;i<kHelpersPerBoss;i++) {
        BossHelper h;
        h.id=kHelperIdBase+(boss.id-kBossIdBase)*kHelpersPerBoss+i; h.boss=boss.id;
        bool found=false;
        for (int attempt=0;attempt<24 && !found;attempt++) {
            const float a=static_cast<float>(rng.Unit())*6.2831853f;
            const float r=170.0f+static_cast<float>(rng.Unit())*170.0f;
            const Vec2 p={boss.home.x+std::sin(a)*r,boss.home.z+std::cos(a)*r};
            found=Inside(p) && (!placement || placement(p)) && (!nav || (nav->Walkable(p) && nav->LineClear(boss.home,p)));
            for (const auto& other:helpers) if (other.alive && Distance(other.home,p)<65.0f) found=false;
            if (found) h.home=h.pos=p;
        }
        if (!found) continue; // never put one in a wall, water or beyond the map
        h.personality=h.moves=static_cast<uint8_t>(i); h.modeAt=clock;
        h.readyAt=clock+1.0f+i*0.6f; h.rot=FaceRot(h.pos,boss.home);
        helpers.push_back(h);
    }
}
bool HelperSight(Vec2 from, Vec2 to) const { return !nav || nav->LineClear(from,to); }
void HelperWalk(BossHelper& h, Vec2 goal, float speed, float dt) {
    Vec2 next=goal;
    if (nav && !nav->LineClear(h.pos,goal)) {
        if (clock>=h.repathAt || Distance(goal,h.pathGoal)>120.0f) {
            h.pathGoal=goal; h.repathAt=clock+0.8f; h.pathIdx=0;
            if (!nav->FindPath(h.pos,goal,h.path)) h.path.clear();
        }
        // Do not skip a corner until the following segment is clear from our actual position.
        while (h.pathIdx<h.path.size() && Distance(h.pos,h.path[h.pathIdx])<25.0f &&
               (h.pathIdx+1==h.path.size() || nav->LineClear(h.pos,h.path[h.pathIdx+1]))) h.pathIdx++;
        if (h.pathIdx==h.path.size()) return;
        next=h.path[h.pathIdx];
    } else { h.path.clear(); h.pathIdx=0; h.repathAt=0; }
    Vec2 p=h.pos;
    Step(p,next,speed*dt);
    const auto clear=[&](Vec2 at) {
        return Inside(at) && (!placement || placement(at)) && (!nav || (nav->Walkable(at) && nav->LineClear(h.pos,at)));
    };
    // A smoothed grid segment can graze a corner between LineClear's samples. Slide along
    // its free axis instead of getting permanently stuck just short of that corner.
    if (!clear(p)) {
        const Vec2 x={p.x,h.pos.z}, z={h.pos.x,p.z};
        if (clear(x) && Distance(x,h.pos)>0.001f) p=x;
        else if (clear(z) && Distance(z,h.pos)>0.001f) p=z;
        else return;
    }
    if (Distance(p,h.pos)>0.001f) {
        h.rot=FaceRot(h.pos,next); h.pos=p;
    }
}
AttackResult AttackHelper(uint32_t attacker, uint32_t target, bool hit, AttackStyle style) {
    AttackResult r;
    PlayerState* a=Find(attacker);
    BossHelper* h=nullptr;
    for (auto& x:helpers) if (x.id==target) h=&x;
    if (state!=MatchState::InMatch || !a || !a->alive || !h || !h->alive || clock<a->stunUntil || clock<a->frozenUntil) return r;
    const bool ammo=HasAmmo(*a,a->weapon.item);
    const WeaponStats w=ActiveWeapon(a->weapon.item,ammo);
    if (w.damage<=0 || clock<a->attackReadyAt || Distance(a->pos,h->pos)>w.range*1.1f+kBokoBodyRadius || !HelperSight(a->pos,h->pos)) return r;
    if (w.ranged) style=AttackStyle::Normal;
    a->attackReadyAt=clock+w.cooldown*(style==AttackStyle::JumpSlash?kJumpSlashRecovery:style==AttackStyle::Spin?kSpinRecovery:1.0f);
    r.ok=true;
    if (ammo) SpendAmmo(*a,a->weapon.item);
    if (!hit) return r;
    const GearTotals gear=TotalsOf(*a);
    r.damage=w.damage*static_cast<float>(Pellets(w,Distance(a->pos,h->pos)))*(ammo?RarityScale(a->weapon.rarity):1.0f)*
        (w.ranged?gear.ranged:gear.melee)*(clock<a->adultUntil?kAdultDamage:1.0f)*(style==AttackStyle::JumpSlash?kJumpSlashDamage:1.0f);
    r.hit=true; a->damageDealt+=(std::min)(r.damage,h->health);
    h->health=(std::max)(0.0f,h->health-r.damage);
    if (h->mode!=BokoMode::Flee) h->target=a->id;
    MatchEvent e{MatchEvent::Type::Damaged}; e.a=h->id; e.b=a->id; e.amount=r.damage; e.health=h->health; events.push_back(e);
    if (h->health<=0) {
        r.killed=true; h->alive=false; h->rockActive=false; HelperMode(*h,BokoMode::Dead);
        // Modest rewards; helpers are neither boss kills nor player eliminations.
        AddLoot({h->pos,ItemId::Rupees,Rarity::Common,false,false});
    } else if (h->mode!=BokoMode::Flee) {
        HelperMode(*h,BokoMode::Hurt,true); h->readyAt=clock+1.0f;
    }
    return r;
}
void TickHelpers(float dt) {
    for (auto& h:helpers) {
        if (!h.alive) {
            if (h.mode==BokoMode::Dead && clock-h.modeAt>BokoDuration(BokoMode::Dead)) HelperMode(h,BokoMode::Idle);
            continue;
        }
        const MiniBoss* boss=FindBoss(h.boss);
        if (!boss || !boss->alive) {
            if (h.mode!=BokoMode::Flee) { HelperMode(h,BokoMode::Flee); h.target=kNoPlayer; h.rockActive=false; }
            if (clock-h.modeAt>=BokoDuration(BokoMode::Flee)) { h.alive=false; continue; }
            const Vec2 origin=boss?boss->pos:h.home;
            float angle=std::atan2(h.pos.x-origin.x,h.pos.z-origin.z);
            if (Distance(origin,h.pos)<1.0f) angle=h.id*2.1f;
            Vec2 away=h.pos;
            // Short, walkable escape steps also work near a map edge or an obstacle.
            for (int turn=0;turn<7;turn++) {
                const float offset=(turn%2?1.0f:-1.0f)*((turn+1)/2)*0.45f;
                const Vec2 p={h.pos.x+std::sin(angle+offset)*90.0f,h.pos.z+std::cos(angle+offset)*90.0f};
                if (Inside(p) && (!placement || placement(p)) && HelperSight(h.pos,p)) { away=p; break; }
            }
            HelperWalk(h,away,130.0f,dt);
            continue;
        }
        if (h.rockActive && clock-h.rockAt>=0.8f) {
            h.rockActive=false;
            for (auto& p:players) if (p.alive && Distance(p.pos,h.rockTo)<=38.0f && HelperSight(h.rockFrom,p.pos)) Blow(p,h.rockFrom,0.35f,h.id);
        }
        const float age=clock-h.modeAt;
        if (h.mode==BokoMode::Hurt || h.mode==BokoMode::Alert || h.mode==BokoMode::Recover || h.mode==BokoMode::Dance) {
            if (age<BokoDuration(h.mode) || (h.mode==BokoMode::Alert && clock<h.readyAt)) continue;
            HelperMode(h,BokoMode::Idle);
        }
        if (BokoAttacking(h.mode)) {
            if (h.mode==BokoMode::Jump) {
                const float t=(std::min)(1.0f,age/BokoDuration(h.mode));
                Vec2 next={h.from.x+(h.aim.x-h.from.x)*t,h.from.z+(h.aim.z-h.from.z)*t};
                if (Inside(next) && (!placement || placement(next)) && HelperSight(h.pos,next)) h.pos=next;
                h.y=(std::max)(0.0f,std::sin(t*3.14159265f)*55.0f);
            }
            const float impact=h.mode==BokoMode::Swing?0.5f:h.mode==BokoMode::Throw?0.7f:BokoDuration(BokoMode::Jump);
            if (!h.resolved && age>=impact) {
                h.resolved=true;
                if (h.mode==BokoMode::Throw) {
                    h.rockFrom=h.pos; h.rockTo=h.aim; h.rockAt=clock; h.rockActive=HelperSight(h.pos,h.aim);
                } else {
                    bool landed=false;
                    for (auto& p:players) {
                        if (!p.alive || clock<p.invulnUntil || Distance(p.pos,h.pos)>kBokoReach || !HelperSight(h.pos,p.pos)) continue;
                        const int16_t off=static_cast<int16_t>(FaceRot(h.pos,p.pos)-h.rot);
                        if (h.mode==BokoMode::Swing && std::abs(static_cast<int>(off))>0x3000) continue;
                        landed=Blow(p,h.pos,h.mode==BokoMode::Jump?0.7f:0.5f,h.id)||landed;
                    }
                    if (!landed && h.mode==BokoMode::Jump) { HelperMode(h,BokoMode::Recover); continue; }
                }
            }
            if (age>=BokoDuration(h.mode)) HelperMode(h,BokoMode::Idle);
            continue;
        }
        PlayerState* target=Find(h.target);
        const auto eligible=[&](const PlayerState* p) { return p && p->alive && clock>=p->invulnUntil && Distance(p->pos,boss->home)<kBokoLeash; };
        if (!eligible(target)) target=nullptr;
        if (!target) {
            PlayerState* commanded=Find(boss->target);
            if (eligible(commanded) && HelperSight(h.pos,commanded->pos)) target=commanded;
        }
        if (!target) {
            float best=kBokoNotice;
            for (auto& p:players) if (eligible(&p) && Distance(h.pos,p.pos)<best && HelperSight(h.pos,p.pos)) { best=Distance(h.pos,p.pos); target=&p; }
        }
        if (target && Distance(h.pos,boss->home)<=kBokoLeash) {
            if (h.target!=target->id) {
                h.target=target->id; HelperMode(h,BokoMode::Alert);
                h.readyAt=(std::max)(h.readyAt,clock+0.9f+h.personality*0.25f); continue;
            }
            h.rot=FaceRot(h.pos,target->pos);
            const float d=Distance(h.pos,target->pos);
            if (clock>=h.readyAt && HelperSight(h.pos,target->pos)) {
                BokoMode attack=BokoMode::Idle;
                if (d<=kBokoReach) attack=BokoMode::Swing;
                // Every helper can open at range; requiring an earlier swing starves both specials.
                else if (d>=100.0f && d<=230.0f && (d<180.0f || h.moves%3!=2)) attack=BokoMode::Jump;
                else if (d>=180.0f && d<=520.0f) attack=BokoMode::Throw;
                if (attack!=BokoMode::Idle) {
                    h.aim=target->pos; h.from=h.pos; h.moves++; HelperMode(h,attack);
                    h.readyAt=clock+BokoDuration(attack)+1.3f+h.personality*0.2f; continue;
                }
            }
            if (d>kBokoReach*0.75f) { HelperMode(h,BokoMode::Walk); HelperWalk(h,target->pos,95.0f,dt); }
            else HelperMode(h,BokoMode::Idle);
        } else {
            h.target=kNoPlayer;
            if (Distance(h.pos,h.home)>25.0f) { HelperMode(h,BokoMode::Walk); HelperWalk(h,h.home,75.0f,dt); }
            else {
                HelperMode(h,BokoMode::Idle);
                // Each has its own phase: the entire camp never dances in lockstep.
                if (clock>=h.readyAt) { HelperMode(h,BokoMode::Dance); h.readyAt=clock+8.0f+h.personality*3.0f; }
            }
        }
    }
}
