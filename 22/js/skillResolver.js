class SkillResolver {
    constructor(mapGenerator) {
        this.mapGenerator = mapGenerator;
    }

    resolveSkill(attacker, defender, skill, allUnits) {
        const result = {
            success: false,
            damage: 0,
            healed: 0,
            effects: [],
            log: '',
            targets: [defender]
        };

        if (skill.type === 'heal') {
            return this.resolveHeal(attacker, defender, skill, allUnits);
        }

        return this.resolveDamage(attacker, defender, skill, allUnits);
    }

    resolveDamage(attacker, defender, skill, allUnits) {
        const result = {
            success: false,
            damage: 0,
            healed: 0,
            effects: [],
            log: '',
            targets: [defender]
        };

        let damage = attacker.atk * skill.damageMultiplier;
        
        const counterBonus = attacker.getCounterBonus(defender.type);
        damage *= counterBonus;
        
        const weaknessPenalty = defender.getWeaknessPenalty(attacker.type);
        damage *= weaknessPenalty;
        
        const terrainDefBonus = this.mapGenerator.getDefenseBonus(defender.x, defender.y);
        damage *= (1 - terrainDefBonus);
        
        if (skill.effect && skill.effect.ignoreDefense) {
            damage = attacker.atk * skill.damageMultiplier * counterBonus * (1 - terrainDefBonus * 0.5);
        }
        
        const actualDamage = defender.takeDamage(damage, skill.type === 'magic');
        result.damage = actualDamage;
        result.success = true;
        
        result.log = `${attacker.name} 使用 ${skill.name} 对 ${defender.name} 造成 ${actualDamage} 点伤害`;
        
        if (counterBonus > 1.0) {
            result.log += ' (克制!)';
        }
        if (weaknessPenalty < 1.0) {
            result.log += ' (被克制!)';
        }
        
        if (skill.effect) {
            if (skill.effect.defReduction) {
                defender.def = Math.max(0, defender.def - defender.baseDef * skill.effect.defReduction);
                result.effects.push({ type: 'def_reduction', target: defender, value: skill.effect.defReduction });
                result.log += `，降低防御`;
            }
            
            if (skill.effect.stun) {
                defender.addStatusEffect({ type: 'stun', stun: true, duration: skill.effect.stun });
                result.effects.push({ type: 'stun', target: defender });
                result.log += '，目标眩晕';
            }
            
            if (skill.effect.poison) {
                defender.addStatusEffect({ type: 'poison', ...skill.effect.poison });
                result.effects.push({ type: 'poison', target: defender });
                result.log += '，目标中毒';
            }
            
            if (skill.effect.moveToTarget) {
                const newPos = this.findAdjacentPosition(attacker, defender);
                if (newPos) {
                    attacker.x = newPos.x;
                    attacker.y = newPos.y;
                    result.log += '，冲锋至目标附近';
                }
            }
            
            if (skill.effect.teleport) {
                const newPos = this.findAdjacentPosition(attacker, defender);
                if (newPos) {
                    attacker.x = newPos.x;
                    attacker.y = newPos.y;
                    result.log += '，瞬移至目标附近';
                }
            }
            
            if (skill.effect.chain) {
                const chainTargets = this.findChainTargets(defender, allUnits, skill.effect.chainCount);
                result.targets = [defender, ...chainTargets];
                
                for (const target of chainTargets) {
                    const chainDamage = attacker.atk * skill.damageMultiplier * 0.7;
                    const actualChainDamage = target.takeDamage(chainDamage, skill.type === 'magic');
                    result.damage += actualChainDamage;
                    result.log += `，对 ${target.name} 造成 ${actualChainDamage} 点连锁伤害`;
                }
            }
        }
        
        return result;
    }

    resolveHeal(attacker, target, skill, allUnits) {
        const result = {
            success: false,
            damage: 0,
            healed: 0,
            effects: [],
            log: '',
            targets: [target]
        };

        if (skill.effect && skill.effect.aoe) {
            const healTargets = this.findAoETargets(attacker, skill.range, allUnits);
            result.targets = healTargets;
            
            for (const t of healTargets) {
                const healAmount = attacker.atk * skill.damageMultiplier;
                const actualHeal = t.heal(healAmount);
                result.healed += actualHeal;
            }
            
            result.success = true;
            result.log = `${attacker.name} 使用 ${skill.name} 恢复 ${healTargets.length} 名友军共 ${result.healed} 点生命`;
        } else {
            const healAmount = attacker.atk * skill.damageMultiplier;
            const actualHeal = target.heal(healAmount);
            
            result.healed = actualHeal;
            result.success = true;
            result.log = `${attacker.name} 使用 ${skill.name} 恢复 ${target.name} ${actualHeal} 点生命`;
        }
        
        return result;
    }

    findAdjacentPosition(unit, target) {
        const directions = [
            { dx: 0, dy: -1 }, { dx: 0, dy: 1 },
            { dx: -1, dy: 0 }, { dx: 1, dy: 0 }
        ];
        
        for (const dir of directions) {
            const x = target.x + dir.dx;
            const y = target.y + dir.dy;
            
            if (this.mapGenerator.isValidPosition(x, y) && 
                this.mapGenerator.isPassable(x, y)) {
                return { x, y };
            }
        }
        
        return null;
    }

    findChainTargets(primaryTarget, allUnits, chainCount) {
        const targets = [];
        const visited = new Set([primaryTarget.id]);
        
        let currentTarget = primaryTarget;
        
        for (let i = 0; i < chainCount; i++) {
            let nearestEnemy = null;
            let nearestDistance = Infinity;
            
            for (const unit of allUnits) {
                if (visited.has(unit.id) || unit.player === primaryTarget.player || unit.hp <= 0) {
                    continue;
                }
                
                const distance = this.mapGenerator.getDistance(currentTarget.x, currentTarget.y, unit.x, unit.y);
                
                if (distance <= 2 && distance < nearestDistance) {
                    nearestDistance = distance;
                    nearestEnemy = unit;
                }
            }
            
            if (nearestEnemy) {
                targets.push(nearestEnemy);
                visited.add(nearestEnemy.id);
                currentTarget = nearestEnemy;
            } else {
                break;
            }
        }
        
        return targets;
    }

    findAoETargets(center, range, allUnits) {
        return allUnits.filter(unit => 
            unit.player === center.player &&
            unit.hp > 0 &&
            this.mapGenerator.getDistance(center.x, center.y, unit.x, unit.y) <= range
        );
    }

    calculateBasicAttack(attacker, defender) {
        let damage = attacker.atk;
        
        const counterBonus = attacker.getCounterBonus(defender.type);
        damage *= counterBonus;
        
        const weaknessPenalty = defender.getWeaknessPenalty(attacker.type);
        damage *= weaknessPenalty;
        
        const terrainDefBonus = this.mapGenerator.getDefenseBonus(defender.x, defender.y);
        damage *= (1 - terrainDefBonus);
        
        const actualDamage = defender.takeDamage(damage);
        
        let log = `${attacker.name} 攻击 ${defender.name} 造成 ${actualDamage} 点伤害`;
        if (counterBonus > 1.0) {
            log += ' (克制!)';
        }
        if (weaknessPenalty < 1.0) {
            log += ' (被克制!)';
        }
        
        return {
            damage: actualDamage,
            log: log
        };
    }

    validateSkillUse(unit, target, skill, allUnits) {
        if (skill.currentCooldown > 0) {
            return { valid: false, reason: '技能冷却中' };
        }
        
        if (skill.type === 'heal' && skill.effect && skill.effect.aoe) {
            const healTargets = this.findAoETargets(unit, skill.range, allUnits);
            if (healTargets.length === 0) {
                return { valid: false, reason: '范围内没有需要治疗的友军' };
            }
            return { valid: true };
        }
        
        const distance = this.mapGenerator.getDistance(unit.x, unit.y, target.x, target.y);
        
        if (distance > skill.range) {
            return { valid: false, reason: `目标超出技能范围(需要${skill.range}格，当前${distance}格)` };
        }
        
        if (skill.type === 'heal') {
            if (target.player !== unit.player) {
                return { valid: false, reason: '只能治疗友方单位' };
            }
        } else {
            if (target.player === unit.player) {
                return { valid: false, reason: '不能攻击友方单位' };
            }
        }
        
        return { valid: true };
    }
}
