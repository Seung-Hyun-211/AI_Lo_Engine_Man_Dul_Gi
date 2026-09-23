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

        // The shape an Area weapon covers at `center` facing `facing` with
        // reach `size` (range for an instant swing, hit_radius for a moving
        // area). The per-shape parameters live in the weapon row; this is the
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

    CombatResult CircularCombat::HitArea(const HitShape& shape, float damage, std::uint8_t defIndex,
                                         const StatBlock& stats, MobField& mobs)
    {
        const std::uint32_t kills = mobs.DamageInShape(shape, damage);
        m_visuals.push_back({ shape, VisualStyle::Outline, defIndex, kOutlineLife, kOutlineLife });
        return Hit(stats, damage, kills);
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

        if (EffectivePath(def) != AttackPath::None)
        {
            SpawnMoving(def, card, context, mobs, damage, CardDamageMax(def, card.level) * dmgMul, range);
            return {};   // damage lands in Step
        }

        switch (spec.form)
        {
        case AttackForm::Area:
            return HitArea(AreaShape(def, spec.shape, playerCenter, context.facing, range, stats[StatId::AttackSize]),
                           damage, card.defIndex, stats, mobs);

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

    void CircularCombat::SpawnMoving(const CardDef& def, const CardInstance& card, const CombatContext& context,
                                     const MobField& mobs, float damage, float damageMax, float range)
    {
        // Aim at the nearest mob in range (D6 - the lurker line too), else
        // along the last move direction.
        const math::Vec2 center = context.playerCenter;
        math::Vec2 aimPos;
        const math::Vec2 aim = mobs.ClosestWithin(center, range, aimPos) ? math::Normalized(aimPos - center) : context.facing;
        if (aim.x == 0.0f && aim.y == 0.0f) return;   // nothing to aim along

        // A Polar cast spreads `count` attacks evenly around the circle;
        // extra_projectiles adds to it. Other paths fire one.
        const bool polar = EffectivePath(def) == AttackPath::Polar;
        const int count = polar ? std::max(1, def.count + static_cast<int>(context.stats[StatId::ExtraProjectiles])) : 1;
        const float baseAngle = std::atan2(aim.y, aim.x);
        for (int i = 0; i < count; ++i)
        {
            if (m_instances.size() >= kMaxInstances) return;   // pool full: drop, not fatal (MobField::Spawn's convention)
            AttackInstance attack;
            attack.defIndex = card.defIndex;
            attack.piercesLeft = static_cast<std::uint8_t>(CardTargets(def, card.level));
            attack.damage = damage;
            attack.damageMax = damageMax;
            attack.scale = context.stats[StatId::AttackSize];
            attack.range = range;
            attack.theta0 = baseAngle + kTwoPi * static_cast<float>(i) / static_cast<float>(count);
            attack.origin = center;
            attack.dir = aim;
            attack.pos = PathPosition(def, attack, center);
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
            result += HitArea(blast, attack.damage, attack.defIndex, context.stats, mobs);
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

    CombatResult CircularCombat::Step(float fixedDelta, const CombatContext& context, MobField& mobs, std::mt19937& rng)
    {
        m_time += fixedDelta;
        CombatResult result;
        for (std::size_t i = 0; i < m_instances.size(); )
        {
            AttackInstance& attack = m_instances[i];
            if (attack.defIndex >= context.balance.weapons.size())   // F5 shrank the weapon table mid-run
            {
                m_instances[i] = m_instances.back();
                m_instances.pop_back();
                continue;
            }
            const CardDef& def = context.balance.weapons[attack.defIndex];
            const EffectSpec& spec = SpecOf(def.effect);

            attack.t += fixedDelta;
            attack.prevPos = attack.pos;
            attack.pos = PathPosition(def, attack, context.playerCenter);

            bool spent = false;
            switch (spec.form)
            {
            case AttackForm::Projectile:
                spent = ResolveContact(attack, def, context, mobs, rng, result);
                break;
            case AttackForm::Area:
            {
                // A moving area hits everything under it once per tick_interval,
                // shaped like its instant form and facing its direction of travel.
                attack.tickLeft -= fixedDelta;
                if (attack.tickLeft <= 0.0f)
                {
                    attack.tickLeft += def.tickInterval;
                    const math::Vec2 moved = attack.pos - attack.prevPos;
                    const math::Vec2 heading = math::Length(moved) > 0.0f ? math::Normalized(moved) : attack.dir;
                    const HitShape shape = AreaShape(def, spec.shape, attack.pos, heading, def.hitRadius * attack.scale, attack.scale);
                    result += HitArea(shape, attack.damage, attack.defIndex, context.stats, mobs);
                }
                break;
            }
            case AttackForm::Nearest:
                spent = true;   // the loader never gives a Nearest weapon a path
                break;
            }

            if (spent || Expired(def, attack)) { m_instances[i] = m_instances.back(); m_instances.pop_back(); }
            else ++i;
        }

        AgeAndErase(m_visuals, fixedDelta);
        return result;
    }
}
