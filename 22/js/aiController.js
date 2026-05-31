class AIController {
    constructor(skillResolver, gamePredictor = null) {
        this.skillResolver = skillResolver;
        this.gamePredictor = gamePredictor;
        this.difficulty = 'hard';
        this.thinkingTime = 300;
        this.positionsEvaluated = 0;
    }

    setDifficulty(level) {
        this.difficulty = level;
        const difficultyConfig = {
            easy: { aggressiveness: 0.5, caution: 1.5, skillUsage: 0.4, thinkingTime: 500 },
            normal: { aggressiveness: 0.8, caution: 1.0, skillUsage: 0.6, thinkingTime: 400 },
            hard: { aggressiveness: 1.0, caution: 0.8, skillUsage: 0.8, thinkingTime: 300 },
            expert: { aggressiveness: 1.2, caution: 0.5, skillUsage: 1.0, thinkingTime: 200 }
        };
        const config = difficultyConfig[level] || difficultyConfig.normal;
        this.aggressiveness = config.aggressiveness;
        this.caution = config.caution;
        this.skillUsage = config.skillUsage;
        this.thinkingTime = config.thinkingTime;
    }

    calculateBestMove(unit, units, mapGenerator) {
        const occupiedTiles = units.filter(u => u.hp > 0 && u.id !== unit.id)
            .map(u => ({ x: u.x, y: u.y }));
        
        const reachable = mapGenerator.calculateReachable(unit, occupiedTiles);
        if (reachable.length === 0) return { x: unit.x, y: unit.y };
        
        const enemies = units.filter(u => u.player !== unit.player && u.hp > 0);
        const allies = units.filter(u => u.player === unit.player && u.hp > 0 && u.id !== unit.id);
        
        let bestPosition = { x: unit.x, y: unit.y };
        let bestScore = -Infinity;
        
        const positionsToEvaluate = this.difficulty === 'expert' ? 
            reachable : reachable.slice(0, Math.min(reachable.length, 10));
        
        for (const tile of positionsToEvaluate) {
            const score = this.evaluatePosition(unit, tile, enemies, allies, mapGenerator);
            
            if (score > bestScore) {
                bestScore = score;
                bestPosition = tile;
            }
        }
        
        const isOccupied = units.some(u => 
            u.hp > 0 && u.id !== unit.id && 
            u.x === bestPosition.x && u.y === bestPosition.y
        );
        
        if (isOccupied) {
            const alternatives = reachable.filter(t => 
                !units.some(u => u.hp > 0 && u.id !== unit.id && u.x === t.x && u.y === t.y)
            );
            if (alternatives.length > 0) {
                let bestAlt = alternatives[0];
                let bestAltScore = -Infinity;
                for (const tile of alternatives) {
                    const score = this.evaluatePosition(unit, tile, enemies, allies, mapGenerator);
                    if (score > bestAltScore) {
                        bestAltScore = score;
                        bestAlt = tile;
                    }
                }
                bestPosition = bestAlt;
            } else {
                bestPosition = { x: unit.x, y: unit.y };
            }
        }
        
        return bestPosition;
    }

    evaluatePosition(unit, position, enemies, allies, mapGenerator) {
        let score = 0;
        
        const terrainDefBonus = mapGenerator.getDefenseBonus(position.x, position.y);
        score += terrainDefBonus * 50;
        
        const terrain = mapGenerator.getTile(position.x, position.y);
        if (terrain) {
            if (terrain.id === 'fortress') score += 40;
            if (terrain.id === 'forest') score += 15;
            if (terrain.id === 'mountain') score += 20;
            if (terrain.id === 'water') score -= 100;
        }
        
        const nearestEnemy = this.findNearestEnemy(position.x, position.y, enemies);
        const nearestAlly = this.findNearestAlly(position.x, position.y, allies);
        
        if (nearestEnemy) {
            const distance = mapGenerator.getDistance(position.x, position.y, nearestEnemy.x, nearestEnemy.y);
            
            if (unit.range > 1) {
                if (distance === unit.range) {
                    score += 100 * this.aggressiveness;
                } else if (distance > unit.range) {
                    score -= (distance - unit.range) * 15;
                } else {
                    score -= (unit.range - distance) * 25;
                }
            } else {
                if (distance === 1) {
                    score += 80 * this.aggressiveness;
                } else {
                    score -= distance * 12;
                }
            }
            
            const counterBonus = unit.getCounterBonus(nearestEnemy.type);
            if (counterBonus > 1.0) {
                score += 40 * this.aggressiveness;
            }
            
            const weaknessPenalty = nearestEnemy.getWeaknessPenalty(unit.type);
            if (weaknessPenalty < 1.0) {
                score -= 50 * this.caution;
            }
            
            const estimatedDamage = this.estimateDamage(unit, nearestEnemy);
            if (estimatedDamage > 0) {
                score += Math.min(estimatedDamage / nearestEnemy.maxHp * 100, 50) * this.aggressiveness;
            }
        }
        
        if (nearestAlly && allies.length > 0) {
            const allyDistance = mapGenerator.getDistance(position.x, position.y, nearestAlly.x, nearestAlly.y);
            if (allyDistance <= 2) {
                score += 15;
            }
        }
        
        if (unit.hp < unit.maxHp * 0.3) {
            score -= 20 * this.caution;
            
            if (nearestAlly && nearestAlly.type === UnitTypes.HEALER) {
                const healerDistance = mapGenerator.getDistance(position.x, position.y, nearestAlly.x, nearestAlly.y);
                if (healerDistance <= 2) {
                    score += 60;
                }
            }
            
            if (nearestEnemy) {
                const enemyDistance = mapGenerator.getDistance(position.x, position.y, nearestEnemy.x, nearestEnemy.y);
                if (enemyDistance <= 2) {
                    score -= 40 * this.caution;
                }
            }
        }
        
        if (unit.type === UnitTypes.HEALER) {
            const woundedAllies = allies.filter(a => a.hp < a.maxHp * 0.7);
            let healScore = 0;
            for (const ally of woundedAllies) {
                const dist = mapGenerator.getDistance(position.x, position.y, ally.x, ally.y);
                if (dist <= unit.range) {
                    healScore += (1 - ally.hp / ally.maxHp) * 80;
                }
            }
            score += healScore;
            
            if (nearestEnemy && mapGenerator.getDistance(position.x, position.y, nearestEnemy.x, nearestEnemy.y) <= 1) {
                score -= 50 * this.caution;
            }
        }
        
        if (this.gamePredictor) {
            const risk = this.evaluatePositionRisk(unit, position, enemies, mapGenerator);
            score -= risk * this.caution;
        }
        
        return score;
    }

    evaluatePositionRisk(unit, position, enemies, mapGenerator) {
        let risk = 0;
        
        for (const enemy of enemies) {
            const distance = mapGenerator.getDistance(position.x, position.y, enemy.x, enemy.y);
            
            if (distance <= enemy.range + enemy.move) {
                const estimatedDamage = this.estimateDamage(enemy, unit);
                const hpPercent = unit.hp / unit.maxHp;
                
                if (estimatedDamage >= unit.hp) {
                    risk += 100;
                } else {
                    risk += (estimatedDamage / unit.maxHp) * 50;
                }
                
                if (enemy.getCounterBonus(unit.type) > 1.0) {
                    risk += 30;
                }
            }
        }
        
        return risk;
    }

    estimateDamage(attacker, defender) {
        let damage = attacker.getEffectiveAtk();
        
        const counterBonus = attacker.getCounterBonus(defender.type);
        damage *= counterBonus;
        
        const weaknessPenalty = defender.getWeaknessPenalty(attacker.type);
        damage *= weaknessPenalty;
        
        const effectiveDef = defender.getEffectiveDef();
        damage = Math.max(1, damage - effectiveDef * 0.5);
        
        return damage;
    }

    findNearestEnemy(x, y, enemies) {
        let nearest = null;
        let minDistance = Infinity;
        
        for (const enemy of enemies) {
            const distance = Math.abs(x - enemy.x) + Math.abs(y - enemy.y);
            if (distance < minDistance) {
                minDistance = distance;
                nearest = enemy;
            }
        }
        
        return nearest;
    }

    findNearestAlly(x, y, allies) {
        if (allies.length === 0) return null;
        
        let nearest = null;
        let minDistance = Infinity;
        
        for (const ally of allies) {
            const distance = Math.abs(x - ally.x) + Math.abs(y - ally.y);
            if (distance < minDistance) {
                minDistance = distance;
                nearest = ally;
            }
        }
        
        return nearest;
    }

    selectBestTarget(unit, units, mapGenerator) {
        const enemies = units.filter(u => u.player !== unit.player && u.hp > 0);
        const attackable = enemies.filter(e => {
            const distance = mapGenerator.getDistance(unit.x, unit.y, e.x, e.y);
            return distance <= unit.range;
        });
        
        if (attackable.length === 0) {
            return null;
        }
        
        let bestTarget = null;
        let bestScore = -Infinity;
        
        for (const target of attackable) {
            let score = 0;
            
            const estimatedDamage = this.estimateDamage(unit, target);
            score += estimatedDamage * this.aggressiveness;
            
            if (estimatedDamage >= target.hp) {
                score += 150 * this.aggressiveness;
            }
            
            const typePriority = {
                [UnitTypes.HEALER]: 80,
                [UnitTypes.MAGE]: 50,
                [UnitTypes.ARCHER]: 40,
                [UnitTypes.ASSASSIN]: 45,
                [UnitTypes.WARRIOR]: 25,
                [UnitTypes.KNIGHT]: 15
            };
            score += typePriority[target.type] || 0;
            
            const counterBonus = unit.getCounterBonus(target.type);
            if (counterBonus > 1.0) {
                score += 60 * this.aggressiveness;
            }
            
            if (target.hp / target.maxHp < 0.3) {
                score += 40 * this.aggressiveness;
            }
            
            if (this.gamePredictor) {
                const prediction = this.gamePredictor.predictBattleOutcome(unit, target, units);
                if (prediction.willKill) {
                    score += 100 * this.aggressiveness;
                }
                score += prediction.expectedDamage * 0.5;
            }
            
            const surroundingEnemies = enemies.filter(e => 
                e.id !== target.id && 
                mapGenerator.getDistance(target.x, target.y, e.x, e.y) <= 1
            );
            score -= surroundingEnemies.length * 10 * this.caution;
            
            if (score > bestScore) {
                bestScore = score;
                bestTarget = target;
            }
        }
        
        return bestTarget;
    }

    selectBestSkill(unit, target, allUnits) {
        const availableSkills = unit.skills.filter(s => s.currentCooldown <= 0);
        
        if (availableSkills.length === 0) {
            return null;
        }
        
        const damagedAllies = allUnits.filter(u => 
            u.player === unit.player && u.hp > 0 && u.hp < u.maxHp
        );
        
        for (const skill of availableSkills) {
            if (skill.type === 'heal') {
                if (skill.effect && skill.effect.aoe) {
                    const healTargets = this.skillResolver.findAoETargets(unit, skill.range, allUnits);
                    if (healTargets.length > 0) {
                        const totalMissingHp = healTargets.reduce((sum, t) => sum + (t.maxHp - t.hp), 0);
                        if (totalMissingHp > unit.atk * skill.damageMultiplier) {
                            return { skill, target: unit };
                        }
                    }
                } else {
                    const lowHpAlly = damagedAllies
                        .filter(a => {
                            const distance = Math.abs(unit.x - a.x) + Math.abs(unit.y - a.y);
                            return distance <= skill.range && a.hp < a.maxHp * 0.4;
                        })
                        .sort((a, b) => a.hp / a.maxHp - b.hp / b.maxHp)[0];
                    
                    if (lowHpAlly) {
                        return { skill, target: lowHpAlly };
                    }
                }
            }
        }
        
        if (!target) return null;
        
        let bestSkill = null;
        let bestScore = -Infinity;
        
        for (const skill of availableSkills) {
            if (skill.type === 'heal') continue;
            
            const validation = this.skillResolver.validateSkillUse(unit, target, skill, allUnits);
            if (!validation.valid) continue;
            
            let score = skill.damageMultiplier * 100 * this.aggressiveness;
            
            if (skill.effect) {
                if (skill.effect.stun) score += 60 * this.aggressiveness;
                if (skill.effect.poison) score += 40 * this.aggressiveness;
                if (skill.effect.chain) score += 50 * this.aggressiveness;
                if (skill.effect.ignoreDefense) score += 40 * this.aggressiveness;
                if (skill.effect.moveToTarget) score += 20;
                if (skill.effect.teleport) score += 25;
            }
            
            const estimatedDamage = unit.getEffectiveAtk() * skill.damageMultiplier;
            if (estimatedDamage >= target.hp) {
                score += 100 * this.aggressiveness;
            }
            
            if (score > bestScore) {
                bestScore = score;
                bestSkill = skill;
            }
        }
        
        const useSkillChance = this.skillUsage * (bestSkill ? bestSkill.damageMultiplier / 2.0 : 0);
        if (bestSkill && Math.random() < useSkillChance) {
            return { skill: bestSkill, target };
        }
        
        return null;
    }

    executeAITurn(unit, units, mapGenerator) {
        return new Promise((resolve) => {
            setTimeout(() => {
                const actions = [];
                
                if (unit.isStunned()) {
                    resolve(actions);
                    return;
                }
                
                if (!unit.hasMoved) {
                    const bestPosition = this.calculateBestMove(unit, units, mapGenerator);
                    
                    if (bestPosition.x !== unit.x || bestPosition.y !== unit.y) {
                        actions.push({
                            type: 'move',
                            from: { x: unit.x, y: unit.y },
                            to: bestPosition
                        });
                        
                        unit.x = bestPosition.x;
                        unit.y = bestPosition.y;
                        unit.hasMoved = true;
                    }
                }
                
                if (!unit.hasActed) {
                    const target = this.selectBestTarget(unit, units, mapGenerator);
                    
                    if (target) {
                        const skillSelection = this.selectBestSkill(unit, target, units);
                        
                        if (skillSelection) {
                            const validation = this.skillResolver.validateSkillUse(
                                unit, skillSelection.target, skillSelection.skill, units
                            );
                            
                            if (validation.valid) {
                                const result = this.skillResolver.resolveSkill(
                                    unit, skillSelection.target, skillSelection.skill, units
                                );
                                
                                unit.useSkill(skillSelection.skill.id);
                                
                                actions.push({
                                    type: 'skill',
                                    skill: skillSelection.skill.id,
                                    target: skillSelection.target.id,
                                    result: result
                                });
                            }
                        } else {
                            const result = this.skillResolver.calculateBasicAttack(unit, target);
                            
                            actions.push({
                                type: 'attack',
                                target: target.id,
                                result: result
                            });
                        }
                        
                        unit.hasActed = true;
                        unit.hasMoved = true;
                    } else {
                        const enemies = units.filter(u => u.player !== unit.player && u.hp > 0);
                        const nearestEnemy = this.findNearestEnemy(unit.x, unit.y, enemies);
                        
                        if (nearestEnemy && unit.hasMoved) {
                            const occupiedTiles = units.filter(u => u.hp > 0 && u.id !== unit.id)
                                .map(u => ({ x: u.x, y: u.y }));
                            const reachable = mapGenerator.calculateReachable(unit, occupiedTiles);
                            
                            let bestMovePos = null;
                            let bestDist = Infinity;
                            
                            for (const tile of reachable) {
                                const dist = mapGenerator.getDistance(tile.x, tile.y, nearestEnemy.x, nearestEnemy.y);
                                if (dist < bestDist) {
                                    bestDist = dist;
                                    bestMovePos = tile;
                                }
                            }
                            
                            if (bestMovePos && (bestMovePos.x !== unit.x || bestMovePos.y !== unit.y)) {
                                actions.push({
                                    type: 'move',
                                    from: { x: unit.x, y: unit.y },
                                    to: bestMovePos
                                });
                                
                                unit.x = bestMovePos.x;
                                unit.y = bestMovePos.y;
                            }
                        }
                    }
                }
                
                resolve(actions);
            }, this.thinkingTime);
        });
    }

    async executeAllAITurns(units, mapGenerator) {
        const aiUnits = units.filter(u => u.player === 2 && u.hp > 0 && !u.isStunned());
        const allActions = [];
        
        const sortedUnits = aiUnits.sort((a, b) => {
            const priority = { [UnitTypes.HEALER]: 0, [UnitTypes.MAGE]: 1, [UnitTypes.ARCHER]: 2, [UnitTypes.ASSASSIN]: 3 };
            return (priority[a.type] || 5) - (priority[b.type] || 5);
        });
        
        for (const unit of sortedUnits) {
            const actions = await this.executeAITurn(unit, units, mapGenerator);
            allActions.push(...actions);
        }
        
        return allActions;
    }
}
