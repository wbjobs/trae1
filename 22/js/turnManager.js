class TurnManager {
    constructor() {
        this.currentTurn = 1;
        this.currentPlayer = 1;
        this.phase = 'idle';
        this.selectedUnit = null;
        this.actionMode = null;
        this.pendingSkill = null;
        this.moveHistory = [];
        this.turnHistory = [];
        this.onTurnStart = null;
        this.onTurnEnd = null;
        this.onPlayerChange = null;
        this.onGameOver = null;
    }

    reset() {
        this.currentTurn = 1;
        this.currentPlayer = 1;
        this.phase = 'idle';
        this.selectedUnit = null;
        this.actionMode = null;
        this.pendingSkill = null;
        this.moveHistory = [];
        this.turnHistory = [];
    }

    startTurn(units, mapGenerator) {
        this.phase = 'select';
        
        const playerUnits = units.filter(u => u.player === this.currentPlayer);
        
        playerUnits.forEach(unit => {
            unit.resetTurn();
            const poisonDamage = unit.processTurnEffects();
        });
        
        this.turnHistory.push({
            turn: this.currentTurn,
            player: this.currentPlayer,
            actions: []
        });
        
        if (this.onTurnStart) {
            this.onTurnStart(this.currentTurn, this.currentPlayer);
        }
    }

    endTurn(units, mapGenerator) {
        const playerUnits = units.filter(u => u.player === this.currentPlayer);
        
        const allActed = playerUnits.every(u => u.hasActed || u.hp <= 0);
        
        this.selectedUnit = null;
        this.actionMode = null;
        this.pendingSkill = null;
        
        if (this.onTurnEnd) {
            this.onTurnEnd(this.currentTurn, this.currentPlayer);
        }
        
        this.switchPlayer();
        
        const winner = this.checkVictory(units);
        if (winner) {
            this.phase = 'game_over';
            if (this.onGameOver) {
                this.onGameOver(winner);
            }
            return winner;
        }
        
        if (this.currentPlayer === 1) {
            this.currentTurn++;
        }
        
        return null;
    }

    switchPlayer() {
        this.currentPlayer = this.currentPlayer === 1 ? 2 : 1;
        
        if (this.onPlayerChange) {
            this.onPlayerChange(this.currentPlayer);
        }
    }

    selectUnit(unit) {
        if (!unit || unit.player !== this.currentPlayer) {
            return false;
        }
        
        if (unit.hp <= 0) {
            return false;
        }
        
        if (unit.isStunned()) {
            return false;
        }
        
        this.selectedUnit = unit;
        this.phase = 'action';
        this.actionMode = null;
        this.pendingSkill = null;
        
        return true;
    }

    deselectUnit() {
        this.selectedUnit = null;
        this.phase = 'select';
        this.actionMode = null;
        this.pendingSkill = null;
    }

    setMoveMode() {
        if (!this.selectedUnit || this.selectedUnit.hasMoved) {
            return false;
        }
        this.actionMode = 'move';
        return true;
    }

    setAttackMode() {
        if (!this.selectedUnit || this.selectedUnit.hasActed) {
            return false;
        }
        this.actionMode = 'attack';
        return true;
    }

    setSkillMode(skillId) {
        if (!this.selectedUnit || this.selectedUnit.hasActed) {
            return false;
        }
        
        const skill = this.selectedUnit.getSkill(skillId);
        if (!skill || skill.currentCooldown > 0) {
            return false;
        }
        
        this.actionMode = 'skill';
        this.pendingSkill = skill;
        return true;
    }

    executeMove(x, y, mapGenerator, occupiedTiles) {
        if (!this.selectedUnit || this.actionMode !== 'move') {
            return { success: false, reason: '无效操作' };
        }
        
        if (this.selectedUnit.hasMoved) {
            return { success: false, reason: '本回合已移动' };
        }
        
        const reachable = mapGenerator.calculateReachable(this.selectedUnit, occupiedTiles);
        const target = reachable.find(t => t.x === x && t.y === y);
        
        if (!target) {
            return { success: false, reason: '目标不可达' };
        }
        
        const oldX = this.selectedUnit.x;
        const oldY = this.selectedUnit.y;
        
        this.selectedUnit.x = x;
        this.selectedUnit.y = y;
        this.selectedUnit.hasMoved = true;
        
        const moveAction = {
            type: 'move',
            unit: this.selectedUnit.id,
            from: { x: oldX, y: oldY },
            to: { x, y }
        };
        
        this.recordAction(moveAction);
        
        return { success: true, oldX, oldY };
    }

    executeAttack(target, skillResolver) {
        if (!this.selectedUnit || this.actionMode !== 'attack') {
            return { success: false, reason: '无效操作' };
        }
        
        if (this.selectedUnit.hasActed) {
            return { success: false, reason: '本回合已行动' };
        }
        
        const distance = Math.abs(this.selectedUnit.x - target.x) + Math.abs(this.selectedUnit.y - target.y);
        
        if (distance > this.selectedUnit.range) {
            return { success: false, reason: '目标超出攻击范围' };
        }
        
        const result = skillResolver.calculateBasicAttack(this.selectedUnit, target);
        
        this.selectedUnit.hasActed = true;
        this.selectedUnit.hasMoved = true;
        
        const attackAction = {
            type: 'attack',
            attacker: this.selectedUnit.id,
            target: target.id,
            damage: result.damage,
            targetDied: target.hp <= 0
        };
        
        this.recordAction(attackAction);
        
        return { success: true, ...result };
    }

    executeSkill(target, skillResolver, allUnits) {
        if (!this.selectedUnit || this.actionMode !== 'skill' || !this.pendingSkill) {
            return { success: false, reason: '无效操作' };
        }
        
        if (this.selectedUnit.hasActed) {
            return { success: false, reason: '本回合已行动' };
        }
        
        const skill = this.pendingSkill;
        
        if (skill.type === 'heal' && skill.effect && skill.effect.aoe) {
            const healTargets = skillResolver.findAoETargets(
                this.selectedUnit, skill.range, allUnits
            );
            
            if (healTargets.length === 0) {
                return { success: false, reason: '范围内没有需要治疗的友军' };
            }
            
            target = this.selectedUnit;
        }
        
        const validation = skillResolver.validateSkillUse(
            this.selectedUnit, target, skill, allUnits
        );
        
        if (!validation.valid) {
            return { success: false, reason: validation.reason };
        }
        
        const result = skillResolver.resolveSkill(
            this.selectedUnit, target, skill, allUnits
        );
        
        this.selectedUnit.useSkill(skill.id);
        
        this.selectedUnit.hasActed = true;
        this.selectedUnit.hasMoved = true;
        
        const skillAction = {
            type: 'skill',
            attacker: this.selectedUnit.id,
            skill: skill.id,
            targets: result.targets.map(t => t.id),
            damage: result.damage,
            healed: result.healed,
            effects: result.effects
        };
        
        this.recordAction(skillAction);
        
        this.pendingSkill = null;
        
        return { success: true, ...result };
    }

    recordAction(action) {
        this.moveHistory.push(action);
        
        if (this.turnHistory.length > 0) {
            this.turnHistory[this.turnHistory.length - 1].actions.push(action);
        }
    }

    undoLastAction(units, mapGenerator) {
        if (this.moveHistory.length === 0) {
            return { success: false, reason: '没有可撤销的操作' };
        }
        
        const lastAction = this.moveHistory.pop();
        
        if (lastAction.type === 'move') {
            const unit = units.find(u => u.id === lastAction.unit);
            if (unit) {
                unit.x = lastAction.from.x;
                unit.y = lastAction.from.y;
                unit.hasMoved = false;
            }
        } else if (lastAction.type === 'attack' || lastAction.type === 'skill') {
            return { success: false, reason: '战斗操作无法撤销' };
        }
        
        if (this.turnHistory.length > 0) {
            this.turnHistory[this.turnHistory.length - 1].actions.pop();
        }
        
        return { success: true };
    }

    checkVictory(units) {
        const player1Units = units.filter(u => u.player === 1 && u.hp > 0);
        const player2Units = units.filter(u => u.player === 2 && u.hp > 0);
        
        if (player1Units.length === 0) {
            return 2;
        }
        
        if (player2Units.length === 0) {
            return 1;
        }
        
        return null;
    }

    getCurrentPlayerUnits(units) {
        return units.filter(u => u.player === this.currentPlayer && u.hp > 0);
    }

    getEnemyUnits(units) {
        const enemyPlayer = this.currentPlayer === 1 ? 2 : 1;
        return units.filter(u => u.player === enemyPlayer && u.hp > 0);
    }

    canEndTurn(units) {
        const playerUnits = this.getCurrentPlayerUnits(units);
        const allActedOrStunned = playerUnits.every(u => u.hasActed || u.isStunned());
        
        return allActedOrStunned || true;
    }

    getTurnState() {
        return {
            turn: this.currentTurn,
            player: this.currentPlayer,
            phase: this.phase,
            selectedUnit: this.selectedUnit ? this.selectedUnit.id : null,
            actionMode: this.actionMode
        };
    }
}
