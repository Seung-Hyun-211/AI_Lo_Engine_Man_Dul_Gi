#include "game/CircularCombat.h"

#include <algorithm>
#include <cmath>

namespace engine::game
{
    namespace
    {
        constexpr float kDegToRad = 3.14159265f / 180.0f;

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

        // The shape an Area weapon covers when fired from `center` facing
        // `facing` with reach `size` (range for an instant swing). The per-shape
        // parameters live in the weapon row; this is the only place that maps
        // them onto a HitShape.
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
    }

    void CircularCombat::Reset()
    {
        m_visuals.clear();
        m_projectiles.clear();
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
                const float life = std::max(kBoltShotMinLife, math::Length(hits[i] - playerCenter) / kBoltShotSpeed);
                m_visuals.push_back({ HitShape::Capsule(playerCenter, hits[i], def.hitRadius), VisualStyle::Travel,
                                      card.defIndex, life, life });
            }
            return Hit(stats, damage, kills);
        }

        case AttackForm::Projectile:
        {
            // Damage lands later, in Step - these actually fly.
            if (m_projectiles.size() >= kMaxProjectiles) return {};   // pool full: drop, not fatal (MobField::Spawn's convention)
            math::Vec2 aimPos;
            const math::Vec2 aimDir = mobs.ClosestWithin(playerCenter, range, aimPos)
                ? math::Normalized(aimPos - playerCenter) : context.facing;
            if (aimDir.x == 0.0f && aimDir.y == 0.0f) return {};   // nothing to aim along

            Projectile shot;
            shot.pos = playerCenter;
            shot.vel = aimDir * def.projectileSpeed;
            shot.rangeLeft = range;
            shot.damage = damage;
            shot.damageMax = CardDamageMax(def, card.level) * dmgMul;
            shot.explodeRadius = def.explodeRadius * stats[StatId::AttackSize];
            shot.piercesLeft = static_cast<std::uint8_t>(CardTargets(def, card.level));
            shot.defIndex = card.defIndex;
            m_projectiles.push_back(shot);
            return {};
        }
        }
        return {};
    }

    CombatResult CircularCombat::Step(float fixedDelta, const CombatContext& context, MobField& mobs, std::mt19937& rng)
    {
        CombatResult result;
        const float reach = context.balance.mobRadius + kProjectileHitReach;
        for (std::size_t i = 0; i < m_projectiles.size(); )
        {
            Projectile& shot = m_projectiles[i];
            const OnHit onHit = SpecOf(context.balance.weapons[shot.defIndex].effect).onHit;
            const float speed = math::Length(shot.vel);
            shot.pos = shot.pos + shot.vel * fixedDelta;
            shot.rangeLeft -= speed * fixedDelta;

            bool spent = false;
            switch (onHit)
            {
            case OnHit::Explode:
            {
                // "닿으면 폭발" (§3.2 스태프) - contact only, not on range-out:
                // read-only proximity check first, then the blast is the hit.
                math::Vec2 hitPos;
                if (mobs.ClosestWithin(shot.pos, reach, hitPos))
                {
                    result += HitArea(HitShape::Circle(hitPos, shot.explodeRadius), shot.damage, shot.defIndex, context.stats, mobs);
                    spent = true;
                }
                break;
            }
            case OnHit::Pierce:
            {
                math::Vec2 hit;
                std::uint32_t hitCount = 0;
                const std::uint32_t hitKills = mobs.DamageNearest(shot.pos, reach, shot.damage, 1, &hit, hitCount);
                if (hitCount > 0)
                {
                    result += Hit(context.stats, shot.damage, hitKills);
                    m_visuals.push_back({ HitShape::Capsule(shot.pos, hit, context.balance.weapons[shot.defIndex].hitRadius), VisualStyle::Travel, shot.defIndex,
                                          kBoltShotMinLife, kBoltShotMinLife });
                    if (shot.piercesLeft > 0) --shot.piercesLeft;
                    if (shot.piercesLeft == 0) spent = true;
                    // Nudge past the mob just hit so next step doesn't immediately
                    // re-hit it (see kPierceClearDistance's comment).
                    else if (speed > 0.0f) shot.pos = shot.pos + shot.vel * (kPierceClearDistance / speed);
                }
                break;
            }
            case OnHit::Random:
            {
                math::Vec2 nearest;
                if (mobs.ClosestWithin(shot.pos, reach, nearest))
                {
                    // Roll only when a hit is about to happen - keeps the RNG
                    // stream from churning on every empty in-flight step.
                    std::uniform_real_distribution<float> roll(shot.damage, std::max(shot.damage, shot.damageMax));
                    const float rolled = roll(rng);
                    math::Vec2 hit;
                    std::uint32_t hitCount = 0;
                    result += Hit(context.stats, rolled, mobs.DamageNearest(shot.pos, reach, rolled, 1, &hit, hitCount));
                    m_visuals.push_back({ HitShape::Capsule(shot.pos, hit, context.balance.weapons[shot.defIndex].hitRadius), VisualStyle::Travel, shot.defIndex,
                                          kBoltShotMinLife, kBoltShotMinLife });
                    spent = true;
                }
                break;
            }
            case OnHit::None:
                break;
            }

            if (spent || shot.rangeLeft <= 0.0f) { m_projectiles[i] = m_projectiles.back(); m_projectiles.pop_back(); }
            else ++i;
        }

        AgeAndErase(m_visuals, fixedDelta);
        return result;
    }
}
