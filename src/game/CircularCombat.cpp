#include "game/CircularCombat.h"

#include <algorithm>
#include <cmath>

namespace engine::game
{
    void CircularCombat::Reset()
    {
        m_pulseRings.clear();
        m_boltShots.clear();
        m_projectiles.clear();
    }

    CombatResult CircularCombat::Hit(const StatBlock& stats, float damage, std::uint32_t kills)
    {
        // Kill-gated (not per point of damage dealt) since the MobField queries
        // only report kill counts, not total damage applied to survivors.
        return { kills, damage * stats[StatId::LifeSteal] * static_cast<float>(kills) };
    }

    CombatResult CircularCombat::Fire(const CardInstance& card, const CombatContext& context, MobField& mobs, std::mt19937& rng)
    {
        const CardDef& def = context.balance.weapons[card.defIndex];
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

        switch (def.effect)
        {
        case CardEffect::RadialPulse:
        {
            const std::uint32_t kills = mobs.DamageInRadius(playerCenter, range, damage);
            m_pulseRings.push_back({ playerCenter, range, kPulseRingLife, kPulseRingLife });
            return Hit(stats, damage, kills);
        }
        case CardEffect::NearestBolt:
        {
            math::Vec2 hits[kMaxBoltTargets];
            std::uint32_t hitCount = 0;
            const int targets = std::min(CardTargets(def, card.level) + static_cast<int>(stats[StatId::ExtraProjectiles]),
                                         kMaxBoltTargets);
            const std::uint32_t kills = mobs.DamageNearest(
                playerCenter, range, damage, static_cast<std::uint32_t>(targets), hits, hitCount);
            // Cosmetic only - the hit already landed above. Duration scales
            // with distance so the square visibly "flies" instead of teleporting.
            for (std::uint32_t i = 0; i < hitCount; ++i)
            {
                const float life = std::max(kBoltShotMinLife, math::Length(hits[i] - playerCenter) / kBoltShotSpeed);
                m_boltShots.push_back({ playerCenter, hits[i], life, life });
            }
            return Hit(stats, damage, kills);
        }
        case CardEffect::ArcSwing:
        {
            const float halfAngleRad = def.coneHalfAngleDeg * (3.14159265f / 180.0f);
            const std::uint32_t kills = mobs.DamageInArc(playerCenter, context.facing, halfAngleRad, range, damage);
            m_pulseRings.push_back({ playerCenter, range, kPulseRingLife, kPulseRingLife });   // stand-in visual until a real swing shape exists
            return Hit(stats, damage, kills);
        }
        case CardEffect::LineSwing:
        {
            const float halfWidth = def.lineHalfWidth * stats[StatId::AttackSize];
            const math::Vec2 lineEnd = playerCenter + context.facing * range;
            const std::uint32_t kills = mobs.DamageInCapsule(playerCenter, lineEnd, halfWidth, damage);
            m_boltShots.push_back({ playerCenter, lineEnd, kPulseRingLife, kPulseRingLife });   // stand-in visual (line sweep)
            return Hit(stats, damage, kills);
        }
        case CardEffect::ExplodingBolt:
        case CardEffect::PiercingShot:
        case CardEffect::RandomDamageShot:
        {
            // Damage lands later, in Step - these three actually fly.
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
            switch (def.effect)
            {
            case CardEffect::RandomDamageShot:
                shot.damageMax = CardDamageMax(def, card.level) * dmgMul;
                shot.kind = ProjectileKind::Random;
                break;
            case CardEffect::PiercingShot:
                shot.piercesLeft = static_cast<std::uint8_t>(CardTargets(def, card.level));
                shot.kind = ProjectileKind::Piercing;
                break;
            default:   // ExplodingBolt
                shot.explodeRadius = def.explodeRadius * stats[StatId::AttackSize];
                shot.kind = ProjectileKind::Exploding;
                break;
            }
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
            const float speed = math::Length(shot.vel);
            shot.pos = shot.pos + shot.vel * fixedDelta;
            shot.rangeLeft -= speed * fixedDelta;

            bool spent = false;
            switch (shot.kind)
            {
            case ProjectileKind::Exploding:
            {
                // "닿으면 폭발" (§3.2 스태프) - contact only, not on range-out:
                // read-only proximity check first, then the AoE is the hit.
                math::Vec2 hitPos;
                if (mobs.ClosestWithin(shot.pos, reach, hitPos))
                {
                    result += Hit(context.stats, shot.damage, mobs.DamageInRadius(hitPos, shot.explodeRadius, shot.damage));
                    m_pulseRings.push_back({ hitPos, shot.explodeRadius, kPulseRingLife, kPulseRingLife });   // stand-in blast ring
                    spent = true;
                }
                break;
            }
            case ProjectileKind::Piercing:
            {
                math::Vec2 hit;
                std::uint32_t hitCount = 0;
                const std::uint32_t hitKills = mobs.DamageNearest(shot.pos, reach, shot.damage, 1, &hit, hitCount);
                if (hitCount > 0)
                {
                    result += Hit(context.stats, shot.damage, hitKills);
                    m_boltShots.push_back({ shot.pos, hit, kBoltShotMinLife, kBoltShotMinLife });
                    if (shot.piercesLeft > 0) --shot.piercesLeft;
                    if (shot.piercesLeft == 0) spent = true;
                    // Nudge past the mob just hit so next step doesn't immediately
                    // re-hit it (see kPierceClearDistance's comment).
                    else if (speed > 0.0f) shot.pos = shot.pos + shot.vel * (kPierceClearDistance / speed);
                }
                break;
            }
            case ProjectileKind::Random:
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
                    m_boltShots.push_back({ shot.pos, hit, kBoltShotMinLife, kBoltShotMinLife });
                    spent = true;
                }
                break;
            }
            }

            if (spent || shot.rangeLeft <= 0.0f) { m_projectiles[i] = m_projectiles.back(); m_projectiles.pop_back(); }
            else ++i;
        }

        // Swap-remove expired attack visuals.
        for (std::size_t i = 0; i < m_pulseRings.size(); )
        {
            m_pulseRings[i].ageLeft -= fixedDelta;
            if (m_pulseRings[i].ageLeft <= 0.0f) { m_pulseRings[i] = m_pulseRings.back(); m_pulseRings.pop_back(); }
            else ++i;
        }
        for (std::size_t i = 0; i < m_boltShots.size(); )
        {
            m_boltShots[i].ageLeft -= fixedDelta;
            if (m_boltShots[i].ageLeft <= 0.0f) { m_boltShots[i] = m_boltShots.back(); m_boltShots.pop_back(); }
            else ++i;
        }
        return result;
    }
}
