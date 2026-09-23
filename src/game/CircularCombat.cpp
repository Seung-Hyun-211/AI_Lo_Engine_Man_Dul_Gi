#include "game/CircularCombat.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace engine::game
{
    namespace
    {
        constexpr float kDegToRad = 3.14159265f / 180.0f;
        constexpr float kTwoPi = 6.28318530718f;

        // Ages every entry by `dt` and swap-removes the expired ones (order is
        // not kept - nothing here depends on it). The one copy of this loop (S6).
        template <typename T>
        void AgeAndErase(std::vector<T>& items, float dt)
        {
            for (std::size_t i = 0; i < items.size(); )
            {
                items[i].ageLeft -= dt;
                if (items[i].ageLeft <= 0.0f) { items[i] = items.back(); items.pop_back(); }
                else ++i;
            }
        }

        // The shape an Area attack covers at `center` facing `facing` with
        // reach `size` (range for an instant swing, hit_radius for a moving
        // area). The per-shape parameters live in the attack's row; this is the
        // only place that maps them onto a HitShape.
        HitShape AreaShape(const CardDef& def, HitShapeKind kind, math::Vec2 center, math::Vec2 facing,
                           float size, float attackSize)
        {
            switch (kind)
            {
            case HitShapeKind::Arc:     return HitShape::Arc(center, facing, def.coneHalfAngleDeg * kDegToRad, size);
            case HitShapeKind::Capsule: return HitShape::Capsule(center, center + facing * size, def.lineHalfWidth * attackSize);
            case HitShapeKind::Circle:
            case HitShapeKind::Count:   break;
            }
            return HitShape::Circle(center, size);
        }

        // --- paths (docs/circular-combat.md §2.5) ---------------------------
        // Position at `attack.t`, computed from scratch every step (never
        // integrated), so the same inputs always give the same track. New path
        // = one AttackPath enumerator + one function in kPaths.
        using PathFn = math::Vec2 (*)(const CardDef&, const AttackInstance&, math::Vec2 anchor);

        math::Vec2 PathNone(const CardDef&, const AttackInstance& attack, math::Vec2)
        {
            return attack.origin;
        }

        math::Vec2 PathStraight(const CardDef& def, const AttackInstance& attack, math::Vec2)
        {
            return attack.origin + attack.dir * (def.projectileSpeed * attack.t);
        }

        math::Vec2 PathPolar(const CardDef& def, const AttackInstance& attack, math::Vec2 anchor)
        {
            const float radius = (def.startRadius + def.radialSpeed * attack.t) * attack.scale;
            const float angle = attack.theta0 + def.angularSpeedDeg * kDegToRad * attack.t;
            return anchor + math::Vec2{ std::cos(angle), std::sin(angle) } * radius;
        }

        constexpr PathFn kPaths[] = { &PathNone, &PathStraight, &PathPolar };
        static_assert(std::size(kPaths) == static_cast<std::size_t>(AttackPath::Count), "one function per AttackPath");

        math::Vec2 PathPosition(const CardDef& def, const AttackInstance& attack, math::Vec2 playerCenter)
        {
            const math::Vec2 anchor = def.anchor == PathAnchor::Player ? playerCenter : attack.origin;
            return kPaths[static_cast<std::size_t>(EffectivePath(def))](def, attack, anchor);
        }

        // Done by time when the row sets a lifetime, else by distance travelled.
        // (A delayed area ends when it lands - Step handles that before this.)
        bool Expired(const CardDef& def, const AttackInstance& attack)
        {
            if (def.lifetime > 0.0f) return attack.t >= def.lifetime;
            return def.projectileSpeed * attack.t >= attack.range;
        }

        // Hit memory (docs §2.2): true while `ref` may not be hit again -
        // forever when rehit_interval is 0 (pierce), else until it elapses.
        bool Remembered(const AttackInstance& attack, MobRef ref, float now, float rehitInterval)
        {
            for (std::uint8_t i = 0; i < attack.memoryCount; ++i)
            {
                const HitMemory& memory = attack.memory[i];
                if (memory.ref == ref) return rehitInterval <= 0.0f || now - memory.time < rehitInterval;
            }
            return false;
        }

        void Remember(AttackInstance& attack, MobRef ref, float now)
        {
            for (std::uint8_t i = 0; i < attack.memoryCount; ++i)
            {
                if (attack.memory[i].ref == ref) { attack.memory[i].time = now; return; }
            }
            attack.memory[attack.memoryNext] = { ref, now };
            attack.memoryNext = static_cast<std::uint8_t>((attack.memoryNext + 1) % AttackInstance::kHitMemory);
            if (attack.memoryCount < AttackInstance::kHitMemory) ++attack.memoryCount;
        }

        // `to - from`, normalized, or `fallback` when the two coincide.
        math::Vec2 AimFrom(math::Vec2 from, math::Vec2 to, math::Vec2 fallback)
        {
            const math::Vec2 d = to - from;
            return math::Length(d) > 1e-4f ? math::Normalized(d) : fallback;
        }
    }

    void CircularCombat::Reset()
    {
        m_time = 0.0f;
        m_visuals.clear();
        m_instances.clear();
    }

    CombatResult CircularCombat::Hit(const StatBlock& stats, float damage, std::uint32_t kills)
    {
        // life_steal (§2.6) heals a fraction of the blow's damage per kill -
        // kill-gated since the MobField queries report kills, not damage dealt.
        return { kills, damage * stats[StatId::LifeSteal] * static_cast<float>(kills) };
    }

    CombatResult CircularCombat::LandArea(Team team, const HitShape& shape, float damage, std::uint8_t defIndex,
                                          const CombatContext& context, MobField& mobs)
    {
        m_visuals.push_back({ shape, VisualStyle::Outline, defIndex, kOutlineLife, kOutlineLife, team });
        switch (team)
        {
        case Team::Player:
            return Hit(context.stats, damage, mobs.DamageInShape(shape, damage));
        case Team::Enemy:
        {
            CombatResult result;
            if (context.playerHittable && ShapeOverlapsRect(shape, context.playerBox)) result.playerDamage = damage;
            return result;
        }
        }
        return {};
    }

    CombatResult CircularCombat::CastArea(Team team, const CardDef& def, std::uint8_t defIndex, math::Vec2 center,
                                          math::Vec2 facing, float size, float scale, float damage,
                                          const CombatContext& context, MobField& mobs)
    {
        const HitShapeKind kind = SpecOf(def.effect).shape;
        if (def.delay <= 0.0f) return LandArea(team, AreaShape(def, kind, center, facing, size, scale), damage, defIndex, context, mobs);

        // Delayed (a caster's hex, a mortar): wait `delay` in place, showing
        // exactly the area that will be hit, then land it (Step).
        if (m_instances.size() >= kMaxInstances) return {};   // pool full: drop, not fatal
        AttackInstance attack;
        attack.team = team;
        attack.defIndex = defIndex;
        attack.damage = damage;
        attack.scale = scale;
        attack.range = size;
        attack.origin = center;
        attack.dir = facing;
        attack.pos = center;
        attack.prevPos = center;
        m_instances.push_back(attack);
        m_visuals.push_back({ AreaShape(def, kind, center, facing, size, scale), VisualStyle::Telegraph, defIndex,
                              def.delay, def.delay, team });
        return {};
    }

    CombatResult CircularCombat::Fire(const CardInstance& card, const CombatContext& context, MobField& mobs, std::mt19937& rng)
    {
        const CardDef& def = context.balance.weapons[card.defIndex];
        const EffectSpec& spec = SpecOf(def.effect);
        const StatBlock& stats = context.stats;
        const math::Vec2 playerCenter = context.playerCenter;
        // Weapons read the stat block by id (docs §2.6): damage x weapon_damage,
        // range x attack_size, bolt targets + extra_projectiles.
        // One crit roll per cast (the whole pulse/volley/shot crits together,
        // not per target) - over the shared run RNG (still deterministic given
        // the same fixed-seed input/timing).
        std::uniform_real_distribution<float> critRoll(0.0f, 1.0f);
        const bool crit = critRoll(rng) < stats[StatId::CritChance];
        const float dmgMul = stats[StatId::WeaponDamage] * (crit ? stats[StatId::CritDamage] : 1.0f);
        const float damage = CardDamage(def, card.level) * dmgMul;
        const float range = CardRange(def, card.level) * stats[StatId::AttackSize];
        const float attackSize = stats[StatId::AttackSize];

        if (EffectivePath(def) != AttackPath::None)
        {
            // Aim at the nearest mob in range (D6 - the lurker line too), else
            // along the last move direction.
            math::Vec2 aimPos;
            const math::Vec2 aim = mobs.ClosestWithin(playerCenter, range, aimPos) ? AimFrom(playerCenter, aimPos, context.facing)
                                                                                    : context.facing;
            const int count = std::max(1, def.count + static_cast<int>(stats[StatId::ExtraProjectiles]));
            SpawnMoving(Team::Player, def, card.defIndex, CardTargets(def, card.level), playerCenter, aim, attackSize, count,
                        damage, CardDamageMax(def, card.level) * dmgMul, range, playerCenter);
            return {};   // damage lands in Step
        }

        switch (spec.form)
        {
        case AttackForm::Area:
        {
            math::Vec2 center = playerCenter;
            math::Vec2 facing = context.facing;
            if (def.origin == AttackOrigin::Target)
            {
                // Lands on the nearest mob in view; nothing to aim at = no cast.
                math::Vec2 targetPos;
                if (!mobs.ClosestWithin(playerCenter, kTargetSearch, targetPos)) return {};
                facing = AimFrom(playerCenter, targetPos, facing);
                center = targetPos;
            }
            return CastArea(Team::Player, def, card.defIndex, center, facing, range, attackSize, damage, context, mobs);
        }

        case AttackForm::Nearest:
        {
            math::Vec2 hits[kMaxBoltTargets];
            std::uint32_t hitCount = 0;
            const int targets = std::min(CardTargets(def, card.level) + static_cast<int>(stats[StatId::ExtraProjectiles]),
                                         kMaxBoltTargets);
            const std::uint32_t kills = mobs.DamageNearest(
                playerCenter, range, damage, static_cast<std::uint32_t>(targets), hits, hitCount);
            // Cosmetic only - the hit already landed above. Duration scales
            // with distance so the dot visibly "flies" instead of teleporting.
            for (std::uint32_t i = 0; i < hitCount; ++i)
            {
                const float life = std::max(kTravelMinLife, math::Length(hits[i] - playerCenter) / kTravelSpeed);
                m_visuals.push_back({ HitShape::Capsule(playerCenter, hits[i], def.hitRadius), VisualStyle::Travel,
                                      card.defIndex, life, life });
            }
            return Hit(stats, damage, kills);
        }

        case AttackForm::Projectile:
            break;   // always has a path (EffectivePath), handled above
        }
        return {};
    }

    CombatResult CircularCombat::FireEnemy(std::uint8_t attackIndex, math::Vec2 caster, const CombatContext& context, MobField& mobs)
    {
        const std::vector<CardDef>& table = context.balance.mobAttacks;
        if (attackIndex >= table.size()) return {};   // F5 shrank mob_attacks.csv mid-run
        const CardDef& def = table[attackIndex];
        const math::Vec2 aim = AimFrom(caster, context.playerCenter, context.facing);
        const float damage = CardDamage(def, 1);
        const float range = CardRange(def, 1);

        if (EffectivePath(def) != AttackPath::None)
        {
            SpawnMoving(Team::Enemy, def, attackIndex, CardTargets(def, 1), caster, aim, 1.0f, std::max(1, def.count),
                        damage, CardDamageMax(def, 1), range, context.playerCenter);
            return {};
        }
        // The loader refuses nearestbolt for enemies, so path None = an Area.
        const math::Vec2 center = def.origin == AttackOrigin::Target ? context.playerCenter : caster;
        return CastArea(Team::Enemy, def, attackIndex, center, aim, range, 1.0f, damage, context, mobs);
    }

    void CircularCombat::SpawnMoving(Team team, const CardDef& def, std::uint8_t defIndex, int pierces, math::Vec2 origin,
                                     math::Vec2 aim, float scale, int count, float damage, float damageMax, float range,
                                     math::Vec2 playerCenter)
    {
        if (aim.x == 0.0f && aim.y == 0.0f) return;   // nothing to aim along

        // A Polar cast spreads `count` attacks evenly around the circle. Other paths fire one.
        if (EffectivePath(def) != AttackPath::Polar) count = 1;
        const float baseAngle = std::atan2(aim.y, aim.x);
        for (int i = 0; i < count; ++i)
        {
            if (m_instances.size() >= kMaxInstances) return;   // pool full: drop, not fatal (MobField::Spawn's convention)
            AttackInstance attack;
            attack.team = team;
            attack.defIndex = defIndex;
            attack.piercesLeft = static_cast<std::uint8_t>(pierces);
            attack.damage = damage;
            attack.damageMax = damageMax;
            attack.scale = scale;
            attack.range = range;
            attack.theta0 = baseAngle + kTwoPi * static_cast<float>(i) / static_cast<float>(count);
            attack.origin = origin;
            attack.dir = aim;
            attack.pos = PathPosition(def, attack, playerCenter);
            attack.prevPos = attack.pos;
            m_instances.push_back(attack);
        }
    }

    bool CircularCombat::ResolveContact(AttackInstance& attack, const CardDef& def, const CombatContext& context,
                                        MobField& mobs, std::mt19937& rng, CombatResult& result)
    {
        // Swept contact (D4): everything the attack's disc passed over this
        // step, each mob by its own radius (P2/P8), minus the ones it must not
        // hit again (P7), nearest-along-the-sweep first.
        const HitShape sweep = HitShape::Capsule(attack.prevPos, attack.pos, def.hitRadius * attack.scale);
        mobs.Overlapping(sweep, m_touchScratch);
        const float rehit = def.rehitInterval;
        const auto removed = std::remove_if(m_touchScratch.begin(), m_touchScratch.end(),
            [&](const MobHit& hit) { return Remembered(attack, hit.ref, m_time, rehit); });
        m_touchScratch.erase(removed, m_touchScratch.end());
        if (m_touchScratch.empty()) return false;

        const math::Vec2 travel = attack.pos - attack.prevPos;
        std::sort(m_touchScratch.begin(), m_touchScratch.end(), [&](const MobHit& a, const MobHit& b) {
            return math::Dot(a.pos - attack.prevPos, travel) < math::Dot(b.pos - attack.prevPos, travel);
        });

        const float hitRadius = def.hitRadius * attack.scale;
        const auto strike = [&](const MobHit& hit, float amount) {
            result += Hit(context.stats, amount, mobs.Damage(hit.ref, amount) ? 1u : 0u);
            Remember(attack, hit.ref, m_time);
            m_visuals.push_back({ HitShape::Capsule(attack.prevPos, hit.pos, hitRadius), VisualStyle::Travel,
                                  attack.defIndex, kTravelMinLife, kTravelMinLife });
        };

        switch (SpecOf(def.effect).onHit)
        {
        case OnHit::Explode:
        {
            // "닿으면 폭발" (§3.2 스태프) - on contact only, never on range-out.
            const HitShape blast = HitShape::Circle(m_touchScratch.front().pos, def.explodeRadius * attack.scale);
            result += LandArea(Team::Player, blast, attack.damage, attack.defIndex, context, mobs);
            return true;
        }
        case OnHit::Random:
        {
            // Rolled only on an actual hit - keeps the RNG stream from churning
            // on every empty in-flight step.
            std::uniform_real_distribution<float> roll(attack.damage, std::max(attack.damage, attack.damageMax));
            strike(m_touchScratch.front(), roll(rng));
            return true;
        }
        case OnHit::Pierce:
        {
            // A lifetime-limited attack (orbit, vortex) keeps cutting until it
            // expires, gated per mob by rehit_interval; a range-limited one is
            // spent after `piercesLeft` hits.
            const bool counted = def.lifetime <= 0.0f;
            for (const MobHit& hit : m_touchScratch)
            {
                strike(hit, attack.damage);
                if (!counted) continue;
                if (attack.piercesLeft > 0) --attack.piercesLeft;
                if (attack.piercesLeft == 0) return true;
            }
            return false;
        }
        case OnHit::None:
            break;
        }
        return false;
    }

    bool CircularCombat::ResolveEnemyContact(AttackInstance& attack, const CardDef& def, const CombatContext& context,
                                             MobField& mobs, std::mt19937& rng, CombatResult& result)
    {
        // Dashing (i-frames [확정]): enemy shots pass straight through.
        if (!context.playerHittable) return false;
        const float hitRadius = def.hitRadius * attack.scale;
        if (!ShapeOverlapsRect(HitShape::Capsule(attack.prevPos, attack.pos, hitRadius), context.playerBox)) return false;

        // There is one target, so every onHit ends the attack on contact.
        float amount = attack.damage;
        switch (SpecOf(def.effect).onHit)
        {
        case OnHit::Explode:
            result += LandArea(Team::Enemy, HitShape::Circle(attack.pos, def.explodeRadius * attack.scale), attack.damage,
                               attack.defIndex, context, mobs);
            return true;
        case OnHit::Random:
        {
            std::uniform_real_distribution<float> roll(attack.damage, std::max(attack.damage, attack.damageMax));
            amount = roll(rng);
            break;
        }
        case OnHit::Pierce:
        case OnHit::None:
            break;
        }
        CombatResult hit;
        hit.playerDamage = amount;
        result += hit;
        m_visuals.push_back({ HitShape::Circle(attack.pos, hitRadius * 2.0f), VisualStyle::Outline, attack.defIndex,
                              kOutlineLife, kOutlineLife, Team::Enemy });
        return true;
    }

    CombatResult CircularCombat::Step(float fixedDelta, const CombatContext& context, MobField& mobs, std::mt19937& rng)
    {
        m_time += fixedDelta;
        CombatResult result;
        for (std::size_t i = 0; i < m_instances.size(); )
        {
            AttackInstance& attack = m_instances[i];
            const std::vector<CardDef>& table = AttackTable(context.balance, attack.team);
            if (attack.defIndex >= table.size())   // F5 shrank the table mid-run
            {
                m_instances[i] = m_instances.back();
                m_instances.pop_back();
                continue;
            }
            const CardDef& def = table[attack.defIndex];
            const EffectSpec& spec = SpecOf(def.effect);
            const AttackPath path = EffectivePath(def);

            attack.t += fixedDelta;
            attack.prevPos = attack.pos;
            attack.pos = PathPosition(def, attack, context.playerCenter);

            bool spent = false;
            switch (spec.form)
            {
            case AttackForm::Projectile:
                spent = attack.team == Team::Player ? ResolveContact(attack, def, context, mobs, rng, result)
                                                    : ResolveEnemyContact(attack, def, context, mobs, rng, result);
                break;
            case AttackForm::Area:
            {
                if (path == AttackPath::None)
                {
                    // A delayed area (CastArea): lands once its delay is up.
                    if (attack.t < def.delay) break;
                    result += LandArea(attack.team, AreaShape(def, spec.shape, attack.origin, attack.dir, attack.range, attack.scale),
                                       attack.damage, attack.defIndex, context, mobs);
                    spent = true;
                    break;
                }
                // A moving area hits everything under it once per tick_interval,
                // shaped like its instant form and facing its direction of travel.
                attack.tickLeft -= fixedDelta;
                if (attack.tickLeft <= 0.0f)
                {
                    attack.tickLeft += def.tickInterval;
                    const math::Vec2 heading = AimFrom(attack.prevPos, attack.pos, attack.dir);
                    const HitShape shape = AreaShape(def, spec.shape, attack.pos, heading, def.hitRadius * attack.scale, attack.scale);
                    result += LandArea(attack.team, shape, attack.damage, attack.defIndex, context, mobs);
                }
                break;
            }
            case AttackForm::Nearest:
                spent = true;   // the loader never gives a Nearest weapon a path
                break;
            }

            if (spent || (path != AttackPath::None && Expired(def, attack))) { m_instances[i] = m_instances.back(); m_instances.pop_back(); }
            else ++i;
        }

        AgeAndErase(m_visuals, fixedDelta);
        return result;
    }
}
