class GamePredictor {
    constructor(mapGenerator, skillResolver) {
        this.mapGenerator = mapGenerator;
        this.skillResolver = skillResolver;
    }

    calculateWinProbability(units) {
        const player1Units = units.filter(u => u.player === 1 && u.hp > 0);
        const player2Units = units.filter(u => u.player === 2 && u.hp > 0);

        if (player1Units.length === 0) return { player1: 0, player2: 100 };
        if (player2Units.length === 0) return { player1: 100, player2: 0 };

        const score1 = this.calculateArmyStrength(player1Units, player2Units);
        const score2 = this.calculateArmyStrength(player2Units, player1Units);

        const totalScore = score1 + score2;
        const player1Prob = totalScore > 0 ? (score1 / totalScore) * 100 : 50;

        return {
            player1: Math.round(player1Prob),
            player2: Math.round(100 - player1Prob),
            details: {
                player1Score: Math.round(score1),
                player2Score: Math.round(score2),
                player1Advantage: score1 > score2
            }
        };
    }

    calculateArmyStrength(units, enemyUnits) {
        let totalStrength = 0;

        for (const unit of units) {
            let unitStrength = this.calculateUnitStrength(unit, enemyUnits);
            totalStrength += unitStrength;
        }

        const avgPosition = this.calculateAveragePosition(units);
        const enemyAvgPosition = this.calculateAveragePosition(enemyUnits);
        const positionAdvantage = this.calculatePositionAdvantage(avgPosition, enemyAvgPosition);

        const terrainBonus = this.calculateTerrainBonus(units);
        const skillBonus = this.calculateSkillBonus(units);

        totalStrength *= (1 + positionAdvantage * 0.1);
        totalStrength *= (1 + terrainBonus * 0.05);
        totalStrength *= (1 + skillBonus * 0.1);

        return totalStrength;
    }

    calculateUnitStrength(unit, enemyUnits) {
        let strength = 0;

        const hpPercent = unit.hp / unit.maxHp;
        strength += unit.maxHp * hpPercent * 0.5;

        const effectiveAtk = unit.getEffectiveAtk();
        strength += effectiveAtk * 3;

        strength += unit.def * 2;

        const traitBonus = this.calculateTraitBonus(unit);
        strength *= (1 + traitBonus * 0.1);

        const rarityBonus = unit.rarity ? unit.rarity.multiplier : 1;
        strength *= rarityBonus;

        const counterBonus = this.calculateCounterPotential(unit, enemyUnits);
        strength *= (1 + counterBonus * 0.1);

        return strength;
    }

    calculateTraitBonus(unit) {
        if (!unit.traits || unit.traits.length === 0) return 0;
        return unit.traits.length * 0.5;
    }

    calculateCounterPotential(unit, enemyUnits) {
        let counterScore = 0;
        for (const enemy of enemyUnits) {
            if (unit.counters.includes(enemy.type)) {
                counterScore += 1;
            }
        }
        return counterScore / Math.max(enemyUnits.length, 1);
    }

    calculateAveragePosition(units) {
        if (units.length === 0) return { x: 0, y: 0 };
        
        let totalX = 0, totalY = 0;
        for (const unit of units) {
            totalX += unit.x;
            totalY += unit.y;
        }
        
        return {
            x: totalX / units.length,
            y: totalY / units.length
        };
    }

    calculatePositionAdvantage(pos1, pos2) {
        const centerX = this.mapGenerator.width / 2;
        const centerY = this.mapGenerator.height / 2;
        
        const dist1 = Math.abs(pos1.x - centerX) + Math.abs(pos1.y - centerY);
        const dist2 = Math.abs(pos2.x - centerX) + Math.abs(pos2.y - centerY);
        
        return (dist2 - dist1) / (this.mapGenerator.width + this.mapGenerator.height);
    }

    calculateTerrainBonus(units) {
        let totalBonus = 0;
        for (const unit of units) {
            const defBonus = this.mapGenerator.getDefenseBonus(unit.x, unit.y);
            totalBonus += defBonus;
        }
        return totalBonus / units.length;
    }

    calculateSkillBonus(units) {
        let bonus = 0;
        for (const unit of units) {
            const availableSkills = unit.skills.filter(s => s.currentCooldown <= 0);
            bonus += availableSkills.length * 0.5;
            
            for (const skill of availableSkills) {
                if (skill.effect) {
                    if (skill.effect.stun || skill.effect.poison) bonus += 0.3;
                    if (skill.effect.chain) bonus += 0.2;
                    if (skill.effect.aoe) bonus += 0.2;
                }
            }
        }
        return bonus;
    }

    predictBattleOutcome(attacker, defender, allUnits) {
        const attackerAtk = attacker.getEffectiveAtk();
        const defenderDef = defender.getEffectiveDef();
        
        let damage = attackerAtk - defenderDef * 0.5;
        
        const counterBonus = attacker.getCounterBonus(defender.type);
        damage *= counterBonus;
        
        const weaknessPenalty = defender.getWeaknessPenalty(attacker.type);
        damage *= weaknessPenalty;
        
        const terrainDefBonus = this.mapGenerator.getDefenseBonus(defender.x, defender.y);
        damage *= (1 - terrainDefBonus);
        
        const critChance = attacker.critChance || 0.1;
        const expectedDamage = damage * (1 - critChance) + damage * (attacker.critDamage || 1.5) * critChance;
        
        const turnsToKill = expectedDamage > 0 ? Math.ceil(defender.hp / expectedDamage) : 999;
        
        return {
            expectedDamage: Math.round(expectedDamage),
            turnsToKill: turnsToKill,
            willKill: expectedDamage >= defender.hp,
            counterActive: counterBonus > 1.0,
            weaknessActive: weaknessPenalty < 1.0
        };
    }

    getStrategicAdvice(units, currentPlayer) {
        const advice = [];
        const playerUnits = units.filter(u => u.player === currentPlayer && u.hp > 0);
        const enemyUnits = units.filter(u => u.player !== currentPlayer && u.hp > 0);
        
        const lowHpUnits = playerUnits.filter(u => u.hp / u.maxHp < 0.3);
        if (lowHpUnits.length > 0) {
            const hasHealer = playerUnits.some(u => u.type === UnitTypes.HEALER);
            advice.push({
                type: hasHealer ? 'heal' : 'retreat',
                message: hasHealer ? 
                    `有 ${lowHpUnits.length} 个单位生命值较低，建议使用治疗技能` :
                    `有 ${lowHpUnits.length} 个单位生命值危急，建议撤退`
            });
        }
        
        for (const unit of playerUnits) {
            const counterTargets = enemyUnits.filter(e => unit.counters.includes(e.type));
            if (counterTargets.length > 0) {
                advice.push({
                    type: 'counter',
                    message: `${unit.name} 克制 ${counterTargets.map(t => t.name).join('、')}，优先攻击`
                });
                break;
            }
        }
        
        const healers = enemyUnits.filter(e => e.type === UnitTypes.HEALER);
        if (healers.length > 0 && playerUnits.some(u => u.range > 1 || u.move >= 3)) {
            advice.push({
                type: 'target_priority',
                message: `优先击杀敌方治疗师: ${healers.map(h => h.name).join('、')}`
            });
        }
        
        const fortresses = this.findNearbyFortresses(playerUnits);
        if (fortresses.length > 0) {
            advice.push({
                type: 'terrain',
                message: `附近有要塞，占领可获得防御加成`
            });
        }
        
        return advice.slice(0, 3);
    }

    findNearbyFortresses(units) {
        const fortresses = [];
        for (let y = 0; y < this.mapGenerator.height; y++) {
            for (let x = 0; x < this.mapGenerator.width; x++) {
                const tile = this.mapGenerator.getTile(x, y);
                if (tile && tile.id === 'fortress') {
                    for (const unit of units) {
                        const dist = this.mapGenerator.getDistance(unit.x, unit.y, x, y);
                        if (dist <= unit.move + 1) {
                            fortresses.push({ x, y, distance: dist });
                            break;
                        }
                    }
                }
            }
        }
        return fortresses;
    }
}
