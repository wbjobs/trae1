class GameStorage {
    constructor() {
        this.storageKey = 'strategy_chess_save';
        this.maxSaves = 10;
    }

    saveGame(gameState, slot = 'auto') {
        try {
            const saves = this.getAllSaves();
            
            const saveData = {
                id: Date.now(),
                slot: slot,
                timestamp: new Date().toISOString(),
                gameState: this.serializeGameState(gameState)
            };
            
            saves[slot] = saveData;
            
            const allSlots = Object.keys(saves);
            if (allSlots.length > this.maxSaves) {
                const sortedSlots = allSlots.sort((a, b) => {
                    return saves[b].timestamp.localeCompare(saves[a].timestamp);
                });
                
                for (let i = this.maxSaves; i < sortedSlots.length; i++) {
                    delete saves[sortedSlots[i]];
                }
            }
            
            localStorage.setItem(this.storageKey, JSON.stringify(saves));
            
            return { success: true, message: '游戏已保存' };
        } catch (error) {
            console.error('保存游戏失败:', error);
            return { success: false, message: '保存失败: ' + error.message };
        }
    }

    loadGame(slot = 'auto') {
        try {
            const saves = this.getAllSaves();
            
            if (!saves[slot]) {
                return { success: false, message: '存档不存在' };
            }
            
            const gameState = this.deserializeGameState(saves[slot].gameState);
            
            return { success: true, gameState, timestamp: saves[slot].timestamp };
        } catch (error) {
            console.error('读取存档失败:', error);
            return { success: false, message: '读取失败: ' + error.message };
        }
    }

    deleteSave(slot) {
        try {
            const saves = this.getAllSaves();
            
            if (!saves[slot]) {
                return { success: false, message: '存档不存在' };
            }
            
            delete saves[slot];
            localStorage.setItem(this.storageKey, JSON.stringify(saves));
            
            return { success: true, message: '存档已删除' };
        } catch (error) {
            console.error('删除存档失败:', error);
            return { success: false, message: '删除失败: ' + error.message };
        }
    }

    getAllSaves() {
        try {
            const data = localStorage.getItem(this.storageKey);
            return data ? JSON.parse(data) : {};
        } catch (error) {
            console.error('读取存档列表失败:', error);
            return {};
        }
    }

    getSaveList() {
        const saves = this.getAllSaves();
        return Object.entries(saves).map(([slot, data]) => ({
            slot,
            id: data.id,
            timestamp: data.timestamp,
            turn: data.gameState.turn,
            player: data.gameState.currentPlayer
        })).sort((a, b) => {
            return new Date(b.timestamp) - new Date(a.timestamp);
        });
    }

    serializeGameState(gameState) {
        return {
            turn: gameState.turn,
            currentPlayer: gameState.currentPlayer,
            units: gameState.units.map(unit => ({
                id: unit.id,
                name: unit.name,
                type: unit.type,
                player: unit.player,
                x: unit.x,
                y: unit.y,
                level: unit.level,
                maxHp: unit.maxHp,
                hp: unit.hp,
                baseAtk: unit.baseAtk,
                atk: unit.atk,
                baseDef: unit.baseDef,
                def: unit.def,
                move: unit.move,
                range: unit.range,
                skills: unit.skills.map(s => ({
                    id: s.id,
                    name: s.name,
                    description: s.description,
                    damageMultiplier: s.damageMultiplier,
                    range: s.range,
                    cooldown: s.cooldown,
                    type: s.type,
                    effect: s.effect,
                    currentCooldown: s.currentCooldown
                })),
                counters: unit.counters,
                weakTo: unit.weakTo,
                color: unit.color,
                hasMoved: unit.hasMoved,
                hasActed: unit.hasActed,
                statusEffects: unit.statusEffects
            })),
            map: gameState.map.map(row => row.map(tile => ({
                id: tile.id,
                name: tile.name,
                color: tile.color,
                moveCost: tile.moveCost,
                defBonus: tile.defBonus,
                passable: tile.passable,
                x: tile.x,
                y: tile.y
            }))),
            moveHistory: gameState.moveHistory,
            turnHistory: gameState.turnHistory,
            replayData: gameState.replayData
        };
    }

    deserializeGameState(data) {
        return {
            turn: data.turn,
            currentPlayer: data.currentPlayer,
            units: data.units.map(u => {
                const template = UnitTemplates[u.type];
                const unit = new Unit(template, u.player, u.x, u.y, u.level);
                
                unit.id = u.id;
                unit.maxHp = u.maxHp;
                unit.hp = u.hp;
                unit.baseAtk = u.baseAtk;
                unit.atk = u.atk;
                unit.baseDef = u.baseDef;
                unit.def = u.def;
                unit.move = u.move;
                unit.range = u.range;
                unit.color = u.color;
                unit.hasMoved = u.hasMoved;
                unit.hasActed = u.hasActed;
                unit.statusEffects = u.statusEffects || [];
                
                unit.skills = u.skills.map(s => ({
                    id: s.id,
                    name: s.name,
                    description: s.description,
                    damageMultiplier: s.damageMultiplier,
                    range: s.range,
                    cooldown: s.cooldown,
                    type: s.type,
                    effect: s.effect,
                    currentCooldown: s.currentCooldown
                }));
                
                return unit;
            }),
            map: data.map.map(row => row.map(tile => ({
                ...TerrainTypes[tile.id.toUpperCase()] || TerrainTypes.PLAIN,
                ...tile
            }))),
            moveHistory: data.moveHistory || [],
            turnHistory: data.turnHistory || [],
            replayData: data.replayData || []
        };
    }

    exportSave(slot = 'auto') {
        const saves = this.getAllSaves();
        
        if (!saves[slot]) {
            return null;
        }
        
        return btoa(JSON.stringify(saves[slot]));
    }

    importSave(encodedData) {
        try {
            const saveData = JSON.parse(atob(encodedData));
            
            const saves = this.getAllSaves();
            saves[saveData.slot || 'imported'] = saveData;
            
            localStorage.setItem(this.storageKey, JSON.stringify(saves));
            
            return { success: true, message: '存档已导入' };
        } catch (error) {
            return { success: false, message: '导入失败: ' + error.message };
        }
    }

    saveReplay(replayData) {
        try {
            const replayKey = 'strategy_chess_replays';
            const replays = this.getAllReplays();
            
            const replay = {
                id: Date.now(),
                timestamp: new Date().toISOString(),
                data: replayData
            };
            
            replays.unshift(replay);
            
            if (replays.length > 10) {
                replays.length = 10;
            }
            
            localStorage.setItem(replayKey, JSON.stringify(replays));
            
            return { success: true, replayId: replay.id };
        } catch (error) {
            console.error('保存回放失败:', error);
            return { success: false };
        }
    }

    loadReplay(replayId) {
        try {
            const replays = this.getAllReplays();
            return replays.find(r => r.id === replayId) || null;
        } catch (error) {
            console.error('加载回放失败:', error);
            return null;
        }
    }

    getAllReplays() {
        try {
            const data = localStorage.getItem('strategy_chess_replays');
            return data ? JSON.parse(data) : [];
        } catch (error) {
            return [];
        }
    }

    deleteReplay(replayId) {
        try {
            const replays = this.getAllReplays();
            const filtered = replays.filter(r => r.id !== replayId);
            localStorage.setItem('strategy_chess_replays', JSON.stringify(filtered));
            return true;
        } catch (error) {
            return false;
        }
    }

    clearAll() {
        localStorage.removeItem(this.storageKey);
        localStorage.removeItem('strategy_chess_replays');
        return { success: true, message: '所有数据已清除' };
    }
}
