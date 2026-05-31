class StatisticsManager {
    constructor() {
        this.storageKey = 'strategy_chess_stats';
        this.globalStats = this.loadGlobalStats();
        this.currentGameStats = this.initializeGameStats();
        this.onStatsUpdate = null;
    }

    initializeGameStats() {
        return {
            startTime: Date.now(),
            turns: 0,
            winner: null,
            players: {
                1: {
                    name: '玩家1',
                    totalDamage: 0,
                    totalHeal: 0,
                    unitsKilled: 0,
                    unitsLost: 0,
                    skillsUsed: 0,
                    criticalHits: 0,
                    units: {}
                },
                2: {
                    name: '玩家2',
                    totalDamage: 0,
                    totalHeal: 0,
                    unitsKilled: 0,
                    unitsLost: 0,
                    skillsUsed: 0,
                    criticalHits: 0,
                    units: {}
                }
            },
            battleLog: [],
            unitHistory: []
        };
    }

    loadGlobalStats() {
        try {
            const data = localStorage.getItem(this.storageKey);
            return data ? JSON.parse(data) : this.initializeGlobalStats();
        } catch (error) {
            console.error('加载统计数据失败:', error);
            return this.initializeGlobalStats();
        }
    }

    initializeGlobalStats() {
        return {
            totalGames: 0,
            totalWins: { 1: 0, 2: 0 },
            totalDamage: { 1: 0, 2: 0 },
            totalKills: { 1: 0, 2: 0 },
            averageGameLength: 0,
            totalGameTime: 0,
            rankings: [],
            achievements: [],
            unitStats: {},
            lastUpdated: Date.now()
        };
    }

    saveGlobalStats() {
        this.globalStats.lastUpdated = Date.now();
        try {
            localStorage.setItem(this.storageKey, JSON.stringify(this.globalStats));
        } catch (error) {
            console.error('保存统计数据失败:', error);
        }
    }

    setPlayerName(player, name) {
        if (this.currentGameStats.players[player]) {
            this.currentGameStats.players[player].name = name;
        }
    }

    recordDamage(player, amount, isCrit = false) {
        const stats = this.currentGameStats.players[player];
        if (stats) {
            stats.totalDamage += amount;
            if (isCrit) {
                stats.criticalHits++;
            }
            this.notifyUpdate();
        }
    }

    recordHeal(player, amount) {
        const stats = this.currentGameStats.players[player];
        if (stats) {
            stats.totalHeal += amount;
            this.notifyUpdate();
        }
    }

    recordKill(killerPlayer, killedPlayer) {
        if (this.currentGameStats.players[killerPlayer]) {
            this.currentGameStats.players[killerPlayer].unitsKilled++;
        }
        if (this.currentGameStats.players[killedPlayer]) {
            this.currentGameStats.players[killedPlayer].unitsLost++;
        }
        this.notifyUpdate();
    }

    recordSkillUse(player) {
        const stats = this.currentGameStats.players[player];
        if (stats) {
            stats.skillsUsed++;
            this.notifyUpdate();
        }
    }

    recordUnitInit(unit) {
        const stats = this.currentGameStats.players[unit.player];
        if (stats && !stats.units[unit.id]) {
            stats.units[unit.id] = {
                id: unit.id,
                name: unit.name,
                type: unit.type,
                rarity: unit.rarity ? unit.rarity.id : 'common',
                level: unit.level,
                maxHp: unit.maxHp,
                atk: unit.baseAtk,
                def: unit.baseDef,
                damageDealt: 0,
                healDone: 0,
                kills: 0,
                survived: true
            };
        }
    }

    recordUnitDamage(unit, amount) {
        const stats = this.currentGameStats.players[unit.player];
        if (stats && stats.units[unit.id]) {
            stats.units[unit.id].damageDealt += amount;
        }
    }

    recordUnitHeal(unit, amount) {
        const stats = this.currentGameStats.players[unit.player];
        if (stats && stats.units[unit.id]) {
            stats.units[unit.id].healDone += amount;
        }
    }

    recordUnitKill(unit) {
        const stats = this.currentGameStats.players[unit.player];
        if (stats && stats.units[unit.id]) {
            stats.units[unit.id].kills++;
        }
    }

    recordUnitDeath(unit) {
        const stats = this.currentGameStats.players[unit.player];
        if (stats && stats.units[unit.id]) {
            stats.units[unit.id].survived = false;
        }
    }

    recordTurnEnd() {
        this.currentGameStats.turns++;
        this.currentGameStats.unitHistory.push({
            turn: this.currentGameStats.turns,
            units: this.snapshotUnits()
        });
    }

    snapshotUnits(units) {
        return units.map(u => ({
            id: u.id,
            player: u.player,
            hp: u.hp,
            maxHp: u.maxHp,
            x: u.x,
            y: u.y
        }));
    }

    recordBattleLog(entry) {
        this.currentGameStats.battleLog.push({
            ...entry,
            timestamp: Date.now()
        });
    }

    endGame(winner, units) {
        this.currentGameStats.winner = winner;
        this.currentGameStats.endTime = Date.now();
        this.currentGameStats.duration = this.currentGameStats.endTime - this.currentGameStats.startTime;

        if (units) {
            for (const unit of units) {
                this.recordUnitDeath(unit);
            }
        }

        this.updateGlobalStats();
        this.updateRankings();

        return this.getGameSummary();
    }

    updateGlobalStats() {
        const stats = this.currentGameStats;
        const winner = stats.winner;

        this.globalStats.totalGames++;
        if (winner) {
            this.globalStats.totalWins[winner]++;
        }

        this.globalStats.totalDamage[1] += stats.players[1].totalDamage;
        this.globalStats.totalDamage[2] += stats.players[2].totalDamage;

        this.globalStats.totalKills[1] += stats.players[1].unitsKilled;
        this.globalStats.totalKills[2] += stats.players[2].unitsKilled;

        const gameTime = (stats.endTime - stats.startTime) / 1000;
        this.globalStats.totalGameTime += gameTime;
        this.globalStats.averageGameLength = 
            this.globalStats.totalGameTime / this.globalStats.totalGames;

        this.updateUnitGlobalStats(stats);

        this.saveGlobalStats();
    }

    updateUnitGlobalStats(gameStats) {
        for (const player of [1, 2]) {
            const units = gameStats.players[player].units;
            for (const unitId in units) {
                const unit = units[unitId];
                if (!this.globalStats.unitStats[unit.type]) {
                    this.globalStats.unitStats[unit.type] = {
                        gamesPlayed: 0,
                        totalDamage: 0,
                        totalHeal: 0,
                        totalKills: 0,
                        wins: 0,
                        losses: 0
                    };
                }

                const globalUnit = this.globalStats.unitStats[unit.type];
                globalUnit.gamesPlayed++;
                globalUnit.totalDamage += unit.damageDealt;
                globalUnit.totalHeal += unit.healDone;
                globalUnit.totalKills += unit.kills;

                if (gameStats.winner === player) {
                    globalUnit.wins++;
                } else {
                    globalUnit.losses++;
                }
            }
        }
    }

    updateRankings() {
        const rankings = [];
        
        for (const player of [1, 2]) {
            const stats = this.currentGameStats.players[player];
            const totalUnits = Object.values(stats.units);
            const bestUnit = totalUnits.sort((a, b) => b.damageDealt - a.damageDealt)[0];

            rankings.push({
                player: player,
                name: stats.name,
                score: this.calculatePlayerScore(stats),
                totalDamage: stats.totalDamage,
                totalHeal: stats.totalHeal,
                unitsKilled: stats.unitsKilled,
                bestUnit: bestUnit ? bestUnit.name : 'N/A',
                turns: this.currentGameStats.turns,
                won: this.currentGameStats.winner === player
            });
        }

        this.globalStats.rankings = rankings.sort((a, b) => b.score - a.score);
        this.saveGlobalStats();
    }

    calculatePlayerScore(playerStats) {
        let score = 0;
        score += playerStats.totalDamage * 0.5;
        score += playerStats.totalHeal * 0.8;
        score += playerStats.unitsKilled * 100;
        score += playerStats.skillsUsed * 20;
        score += playerStats.criticalHits * 30;
        score -= playerStats.unitsLost * 50;
        return Math.round(score);
    }

    getGameSummary() {
        const stats = this.currentGameStats;
        return {
            winner: stats.winner,
            turns: stats.turns,
            duration: Math.round((stats.endTime - stats.startTime) / 1000),
            players: {
                1: {
                    ...stats.players[1],
                    score: this.calculatePlayerScore(stats.players[1]),
                    units: Object.values(stats.players[1].units)
                },
                2: {
                    ...stats.players[2],
                    score: this.calculatePlayerScore(stats.players[2]),
                    units: Object.values(stats.players[2].units)
                }
            }
        };
    }

    getGlobalStats() {
        return this.globalStats;
    }

    getRankings() {
        return this.globalStats.rankings || [];
    }

    getUnitStatistics(unitType) {
        return this.globalStats.unitStats[unitType] || {
            gamesPlayed: 0,
            totalDamage: 0,
            totalHeal: 0,
            totalKills: 0,
            wins: 0,
            losses: 0,
            winRate: 0
        };
    }

    getAllUnitStatistics() {
        return this.globalStats.unitStats;
    }

    getAchievements() {
        const achievements = [];
        const stats = this.globalStats;

        if (stats.totalGames >= 1) {
            achievements.push({ id: 'first_game', name: '初出茅庐', description: '完成第一场游戏' });
        }
        if (stats.totalWins[1] >= 5 || stats.totalWins[2] >= 5) {
            achievements.push({ id: 'winning_streak', name: '常胜将军', description: '累计获胜5场' });
        }
        if (stats.totalWins[1] >= 10 || stats.totalWins[2] >= 10) {
            achievements.push({ id: 'battle_hardened', name: '身经百战', description: '累计获胜10场' });
        }
        if (stats.totalDamage[1] >= 1000 || stats.totalDamage[2] >= 1000) {
            achievements.push({ id: 'damage_dealer', name: '伤害大师', description: '累计造成1000点伤害' });
        }
        if (stats.totalKills[1] >= 20 || stats.totalKills[2] >= 20) {
            achievements.push({ id: 'fierce_warrior', name: ' fierce', description: '累计击杀20个单位' });
        }

        return achievements;
    }

    notifyUpdate() {
        if (this.onStatsUpdate) {
            this.onStatsUpdate(this.getCurrentGameStats());
        }
    }

    getCurrentGameStats() {
        return {
            turns: this.currentGameStats.turns,
            players: {
                1: {
                    totalDamage: this.currentGameStats.players[1].totalDamage,
                    totalHeal: this.currentGameStats.players[1].totalHeal,
                    unitsKilled: this.currentGameStats.players[1].unitsKilled,
                    unitsLost: this.currentGameStats.players[1].unitsLost,
                    criticalHits: this.currentGameStats.players[1].criticalHits
                },
                2: {
                    totalDamage: this.currentGameStats.players[2].totalDamage,
                    totalHeal: this.currentGameStats.players[2].totalHeal,
                    unitsKilled: this.currentGameStats.players[2].unitsKilled,
                    unitsLost: this.currentGameStats.players[2].unitsLost,
                    criticalHits: this.currentGameStats.players[2].criticalHits
                }
            }
        };
    }

    resetGameStats() {
        this.currentGameStats = this.initializeGameStats();
    }

    clearAllStats() {
        this.globalStats = this.initializeGlobalStats();
        this.saveGlobalStats();
        this.resetGameStats();
    }

    exportStats() {
        return btoa(JSON.stringify(this.globalStats));
    }

    importStats(encodedData) {
        try {
            const data = JSON.parse(atob(encodedData));
            this.globalStats = { ...this.initializeGlobalStats(), ...data };
            this.saveGlobalStats();
            return true;
        } catch (error) {
            console.error('导入统计数据失败:', error);
            return false;
        }
    }
}
