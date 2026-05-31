class Game {
    constructor() {
        this.canvas = document.getElementById('game-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.tileSize = GameConfig.tileSize;
        
        this.mapGenerator = new MapGenerator(GameConfig.mapWidth, GameConfig.mapHeight);
        this.skillResolver = new SkillResolver(this.mapGenerator);
        this.turnManager = new TurnManager();
        this.gamePredictor = new GamePredictor(this.mapGenerator, this.skillResolver);
        this.statisticsManager = new StatisticsManager();
        this.aiController = new AIController(this.skillResolver, this.gamePredictor);
        this.gameStorage = new GameStorage();
        this.networkManager = new NetworkManager();
        this.replayManager = null;
        
        this.units = [];
        this.mapData = null;
        this.selectedUnit = null;
        this.reachableTiles = [];
        this.attackableTiles = [];
        
        this.aiEnabled = false;
        this.isReplaying = false;
        this.replayData = [];
        this.replayFrames = [];
        
        this.hoveredTile = null;
        this.winProbability = { player1: 50, player2: 50 };
        
        this.init();
    }

    init() {
        this.setupEventListeners();
        this.resetGame();
        this.updateUI();
        this.render();
    }

    setupEventListeners() {
        this.canvas.addEventListener('click', (e) => this.handleCanvasClick(e));
        this.canvas.addEventListener('mousemove', (e) => this.handleMouseMove(e));
        this.canvas.addEventListener('mouseleave', () => {
            this.hoveredTile = null;
            this.render();
        });
        
        document.getElementById('btn-start').addEventListener('click', () => this.startGame());
        document.getElementById('btn-restart').addEventListener('click', () => this.restartGame());
        document.getElementById('btn-save').addEventListener('click', () => this.saveGame());
        document.getElementById('btn-load').addEventListener('click', () => this.loadGame());
        document.getElementById('btn-undo').addEventListener('click', () => this.undoAction());
        document.getElementById('btn-end-turn').addEventListener('click', () => this.endTurn());
        document.getElementById('btn-ai-toggle').addEventListener('click', () => this.toggleAI());
        document.getElementById('btn-replay').addEventListener('click', () => this.showReplayModal());
        document.getElementById('btn-multiplayer').addEventListener('click', () => this.showMultiplayerModal());
        document.getElementById('btn-stats').addEventListener('click', () => this.showStatsModal());
        
        document.getElementById('btn-create-room').addEventListener('click', () => this.createRoom());
        document.getElementById('btn-join-room').addEventListener('click', () => this.joinRoom());
        document.getElementById('btn-close-multiplayer').addEventListener('click', () => this.hideMultiplayerModal());
        
        document.getElementById('btn-replay-play').addEventListener('click', () => this.playReplay());
        document.getElementById('btn-replay-pause').addEventListener('click', () => this.pauseReplay());
        document.getElementById('btn-replay-speed').addEventListener('click', () => this.changeReplaySpeed());
        document.getElementById('btn-replay-close').addEventListener('click', () => this.closeReplay());
        
        document.getElementById('btn-close-stats').addEventListener('click', () => this.hideStatsModal());
        
        this.turnManager.onGameOver = (winner) => this.handleGameOver(winner);
    }

    resetGame() {
        const result = this.mapGenerator.generate();
        this.mapData = result.map;
        this.units = [];
        this.replayFrames = [];
        
        this.canvas.width = this.mapGenerator.width * this.tileSize;
        this.canvas.height = this.mapGenerator.height * this.tileSize;
        
        this.generateUnits(result.player1Start, result.player2Start);
        this.turnManager.reset();
        this.statisticsManager.resetGameStats();
        this.selectedUnit = null;
        this.reachableTiles = [];
        this.attackableTiles = [];
        
        this.updateWinProbability();
        
        this.captureFrame(null, null);
        
        this.addLog('游戏开始！红方先行', 'log-skill');
    }

    generateUnits(player1Start, player2Start) {
        const unitTypes = [
            UnitTypes.WARRIOR,
            UnitTypes.ARCHER,
            UnitTypes.MAGE,
            UnitTypes.KNIGHT,
            UnitTypes.HEALER
        ];
        
        for (let i = 0; i < Math.min(5, player1Start.length); i++) {
            const type = unitTypes[i % unitTypes.length];
            const template = UnitTemplates[type];
            const pos = player1Start[i];
            
            const unit = new Unit(template, 1, pos.x, pos.y, 1);
            
            if (GameConfig.enableRandomAttributes) {
                UnitRandomizer.randomizeUnit(unit);
                UnitRandomizer.applyTraitEffects(unit);
            }
            
            this.statisticsManager.recordUnitInit(unit);
            this.units.push(unit);
        }
        
        for (let i = 0; i < Math.min(5, player2Start.length); i++) {
            const type = unitTypes[i % unitTypes.length];
            const template = UnitTemplates[type];
            const pos = player2Start[i];
            
            const unit = new Unit(template, 2, pos.x, pos.y, 1);
            
            if (GameConfig.enableRandomAttributes) {
                UnitRandomizer.randomizeUnit(unit);
                UnitRandomizer.applyTraitEffects(unit);
            }
            
            this.statisticsManager.recordUnitInit(unit);
            this.units.push(unit);
        }
    }

    startGame() {
        this.resetGame();
        this.turnManager.phase = 'select';
        this.updateUI();
        this.render();
        this.addLog('游戏开始！', 'log-skill');
    }

    restartGame() {
        this.startGame();
    }

    handleCanvasClick(e) {
        if (this.isReplaying) return;
        
        const rect = this.canvas.getBoundingClientRect();
        const x = Math.floor((e.clientX - rect.left) / this.tileSize);
        const y = Math.floor((e.clientY - rect.top) / this.tileSize);
        
        if (!this.mapGenerator.isValidPosition(x, y)) return;
        
        const clickedUnit = this.getUnitAt(x, y);
        
        if (this.turnManager.selectedUnit) {
            if (this.turnManager.actionMode === 'move') {
                this.handleMoveClick(x, y, clickedUnit);
            } else if (this.turnManager.actionMode === 'attack') {
                this.handleAttackClick(x, y, clickedUnit);
            } else if (this.turnManager.actionMode === 'skill') {
                this.handleSkillClick(x, y, clickedUnit);
            } else {
                if (clickedUnit && clickedUnit.player === this.turnManager.currentPlayer) {
                    this.selectUnit(clickedUnit);
                } else if (clickedUnit && clickedUnit.player !== this.turnManager.currentPlayer) {
                    this.turnManager.setAttackMode();
                    this.handleAttackClick(x, y, clickedUnit);
                } else {
                    this.deselectUnit();
                }
            }
        } else {
            if (clickedUnit && clickedUnit.player === this.turnManager.currentPlayer) {
                this.selectUnit(clickedUnit);
            }
        }
    }

    handleMouseMove(e) {
        if (this.isReplaying) return;
        
        const rect = this.canvas.getBoundingClientRect();
        const x = Math.floor((e.clientX - rect.left) / this.tileSize);
        const y = Math.floor((e.clientY - rect.top) / this.tileSize);
        
        if (this.mapGenerator.isValidPosition(x, y)) {
            this.hoveredTile = { x, y };
        } else {
            this.hoveredTile = null;
        }
        
        this.render();
    }

    handleMoveClick(x, y, clickedUnit) {
        const occupiedTiles = this.units.filter(u => u.hp > 0)
            .map(u => ({ x: u.x, y: u.y }));
        
        const result = this.turnManager.executeMove(x, y, this.mapGenerator, occupiedTiles);
        
        if (result.success) {
            this.addLog(`${this.turnManager.selectedUnit.name} 移动到 (${x}, ${y})`, 'log-move');
            this.captureFrame({ selected: { x, y } }, null);
            this.turnManager.actionMode = null;
            this.updateReachableTiles();
            this.updateWinProbability();
            this.updateUI();
        } else {
            this.addLog(result.reason, 'log-damage');
        }
        
        this.render();
    }

    handleAttackClick(x, y, clickedUnit) {
        if (!clickedUnit || clickedUnit.player === this.turnManager.currentPlayer) {
            if (clickedUnit && clickedUnit.player === this.turnManager.currentPlayer) {
                this.selectUnit(clickedUnit);
            }
            return;
        }
        
        const attacker = this.turnManager.selectedUnit;
        const result = this.turnManager.executeAttack(clickedUnit, this.skillResolver);
        
        if (result.success) {
            this.addLog(result.log, 'log-damage');
            
            this.statisticsManager.recordDamage(attacker.player, result.damage);
            this.statisticsManager.recordUnitDamage(attacker, result.damage);
            
            if (result.targetDied || clickedUnit.hp <= 0) {
                this.addLog(`${clickedUnit.name} 被击败！`, 'log-skill');
                this.statisticsManager.recordKill(attacker.player, clickedUnit.player);
                this.statisticsManager.recordUnitKill(attacker);
                this.statisticsManager.recordUnitDeath(clickedUnit);
                this.removeDeadUnits();
            }
            
            const highlights = {
                selected: { x: attacker.x, y: attacker.y }
            };
            this.captureFrame(highlights, result.log);
            
            this.turnManager.actionMode = null;
            this.updateWinProbability();
            this.checkAutoEndTurn();
            this.updateUI();
        } else {
            this.addLog(result.reason, 'log-damage');
        }
        
        this.render();
    }

    handleSkillClick(x, y, clickedUnit) {
        const pendingSkill = this.turnManager.pendingSkill;
        if (!pendingSkill) {
            this.addLog('请先选择技能', 'log-damage');
            return;
        }
        
        const attacker = this.turnManager.selectedUnit;

        if (pendingSkill.type === 'heal') {
            if (pendingSkill.effect && pendingSkill.effect.aoe) {
                const result = this.turnManager.executeSkill(attacker, this.skillResolver, this.units);
                
                if (result.success) {
                    this.addLog(result.log, 'log-heal');
                    this.statisticsManager.recordHeal(attacker.player, result.healed);
                    this.statisticsManager.recordSkillUse(attacker.player);
                    this.captureFrame({ selected: { x: attacker.x, y: attacker.y } }, result.log);
                    this.turnManager.actionMode = null;
                    this.turnManager.pendingSkill = null;
                    this.updateWinProbability();
                    this.checkAutoEndTurn();
                    this.updateUI();
                } else {
                    this.addLog(result.reason, 'log-damage');
                }
                this.render();
                return;
            }

            if (!clickedUnit) {
                this.addLog('请选择要治疗的友方单位', 'log-damage');
                return;
            }

            if (clickedUnit.player !== this.turnManager.currentPlayer) {
                this.addLog('只能治疗友方单位', 'log-damage');
                return;
            }
        } else {
            if (!clickedUnit) {
                this.addLog('请选择攻击目标', 'log-damage');
                return;
            }

            if (clickedUnit.player === this.turnManager.currentPlayer) {
                this.addLog('不能攻击友方单位', 'log-damage');
                return;
            }
        }
        
        const result = this.turnManager.executeSkill(clickedUnit, this.skillResolver, this.units);
        
        if (result.success) {
            if (result.healed > 0) {
                this.addLog(result.log, 'log-heal');
                this.statisticsManager.recordHeal(attacker.player, result.healed);
            } else {
                this.addLog(result.log, 'log-skill');
                this.statisticsManager.recordDamage(attacker.player, result.damage);
                this.statisticsManager.recordUnitDamage(attacker, result.damage);
            }
            
            this.statisticsManager.recordSkillUse(attacker.player);
            
            if (result.damage > 0) {
                result.targets.forEach(t => {
                    if (t.hp <= 0) {
                        this.addLog(`${t.name} 被击败！`, 'log-skill');
                        this.statisticsManager.recordKill(attacker.player, t.player);
                        this.statisticsManager.recordUnitKill(attacker);
                        this.statisticsManager.recordUnitDeath(t);
                    }
                });
                this.removeDeadUnits();
            }
            
            const highlights = {
                selected: { x: attacker.x, y: attacker.y }
            };
            this.captureFrame(highlights, result.log);
            
            this.turnManager.actionMode = null;
            this.turnManager.pendingSkill = null;
            this.updateWinProbability();
            this.checkAutoEndTurn();
            this.updateUI();
        } else {
            this.addLog(result.reason, 'log-damage');
        }
        
        this.render();
    }

    selectUnit(unit) {
        if (this.turnManager.selectUnit(unit)) {
            this.selectedUnit = unit;
            this.updateReachableTiles();
            this.updateAttackableTiles();
            
            if (!unit.hasMoved) {
                this.turnManager.setMoveMode();
            } else if (!unit.hasActed) {
                this.turnManager.setAttackMode();
            }
            
            this.updateUI();
            this.render();
        }
    }

    deselectUnit() {
        this.turnManager.deselectUnit();
        this.selectedUnit = null;
        this.reachableTiles = [];
        this.attackableTiles = [];
        this.updateUI();
        this.render();
    }

    updateReachableTiles() {
        if (!this.selectedUnit || this.selectedUnit.hasMoved) {
            this.reachableTiles = [];
            return;
        }
        
        const occupiedTiles = this.units.filter(u => u.hp > 0)
            .map(u => ({ x: u.x, y: u.y }));
        
        this.reachableTiles = this.mapGenerator.calculateReachable(
            this.selectedUnit, occupiedTiles
        );
    }

    updateAttackableTiles() {
        if (!this.selectedUnit) {
            this.attackableTiles = [];
            return;
        }
        
        this.attackableTiles = this.mapGenerator.calculateAttackRange(this.selectedUnit);
    }

    useSkill(skillId) {
        if (!this.selectedUnit || this.selectedUnit.hasActed) return;
        
        const skill = this.selectedUnit.getSkill(skillId);
        if (!skill || skill.currentCooldown > 0) return;
        
        this.turnManager.setSkillMode(skillId);
        
        if (skill.type === 'heal' && skill.effect && skill.effect.aoe) {
            this.addLog(`${skill.name} 将治疗范围内所有友军，请确认使用`, 'log-skill');
        } else if (skill.type === 'heal') {
            this.addLog(`选择 ${skill.name} 的治疗目标`, 'log-skill');
        } else {
            this.addLog(`选择 ${skill.name} 的攻击目标`, 'log-skill');
        }
        
        this.updateUI();
        this.render();
    }

    endTurn() {
        this.statisticsManager.recordTurnEnd();
        
        const result = this.turnManager.endTurn(this.units, this.mapGenerator);
        
        this.captureFrame(null, `回合 ${this.turnManager.currentTurn} 结束`);
        
        if (result) {
            this.handleGameOver(result);
            return;
        }
        
        this.addLog(`回合 ${this.turnManager.currentTurn} - ${this.turnManager.currentPlayer === 1 ? '红方' : '蓝方'}`, 'log-skill');
        
        this.turnManager.startTurn(this.units, this.mapGenerator);
        
        this.selectedUnit = null;
        this.reachableTiles = [];
        this.attackableTiles = [];
        
        this.updateWinProbability();
        this.captureFrame(null, `回合 ${this.turnManager.currentTurn} 开始`);
        
        if (this.aiEnabled && this.turnManager.currentPlayer === 2) {
            this.executeAITurn();
        }
        
        this.updateUI();
        this.render();
    }

    async executeAITurn() {
        this.addLog('AI思考中...', 'log-skill');
        
        const aiActions = await this.aiController.executeAllAITurns(this.units, this.mapGenerator);
        
        for (const action of aiActions) {
            if (action.type === 'move') {
                const unit = this.units.find(u => u.x === action.to.x && u.y === action.to.y && u.player === 2);
                const unitName = unit ? unit.name : '单位';
                this.addLog(`AI: ${unitName} 移动`, 'log-move');
            } else if (action.type === 'attack') {
                this.addLog(`AI: ${action.result?.log || '攻击'}`, 'log-damage');
                if (action.result?.damage) {
                    this.statisticsManager.recordDamage(2, action.result.damage);
                }
            } else if (action.type === 'skill') {
                this.addLog(`AI: ${action.result?.log || '技能'}`, 'log-skill');
                this.statisticsManager.recordSkillUse(2);
                if (action.result?.damage) {
                    this.statisticsManager.recordDamage(2, action.result.damage);
                }
                if (action.result?.healed) {
                    this.statisticsManager.recordHeal(2, action.result.healed);
                }
            }
        }
        
        this.removeDeadUnits();
        this.updateWinProbability();
        
        const winner = this.turnManager.checkVictory(this.units);
        if (winner) {
            this.handleGameOver(winner);
            return;
        }
        
        this.captureFrame(null, 'AI回合结束');
        
        this.endTurn();
    }

    checkAutoEndTurn() {
        const playerUnits = this.turnManager.getCurrentPlayerUnits(this.units);
        const allActed = playerUnits.every(u => u.hasActed || u.isStunned());
        
        if (allActed) {
            setTimeout(() => this.endTurn(), 500);
        }
    }

    getUnitAt(x, y) {
        return this.units.find(u => u.x === x && u.y === y && u.hp > 0);
    }

    getUnitById(id) {
        return this.units.find(u => u.id === id);
    }

    removeDeadUnits() {
        this.units = this.units.filter(u => u.hp > 0);
    }

    handleGameOver(winner) {
        const winnerName = winner === 1 ? '红方' : '蓝方';
        this.addLog(`游戏结束！${winnerName} 获胜！`, 'log-skill');
        
        this.statisticsManager.endGame(winner, this.units);
        
        this.saveReplay();
        
        this.showGameOverModal(winner);
        
        this.turnManager.phase = 'game_over';
        this.updateUI();
        this.render();
    }

    showGameOverModal(winner) {
        const summary = this.statisticsManager.getGameSummary();
        const winnerName = winner === 1 ? '红方' : '蓝方';
        
        const modal = document.getElementById('game-over-modal');
        document.getElementById('game-over-winner').textContent = `${winnerName} 获胜！`;
        document.getElementById('game-over-turns').textContent = `回合数: ${summary.turns}`;
        document.getElementById('game-over-duration').textContent = `时长: ${summary.duration}秒`;
        
        const player1Stats = document.getElementById('game-over-player1-stats');
        const player2Stats = document.getElementById('game-over-player2-stats');
        
        player1Stats.innerHTML = `
            <p>总伤害: ${summary.players[1].totalDamage}</p>
            <p>总治疗: ${summary.players[1].totalHeal}</p>
            <p>击杀数: ${summary.players[1].unitsKilled}</p>
            <p>技能使用: ${summary.players[1].skillsUsed}</p>
            <p>得分: ${summary.players[1].score}</p>
        `;
        
        player2Stats.innerHTML = `
            <p>总伤害: ${summary.players[2].totalDamage}</p>
            <p>总治疗: ${summary.players[2].totalHeal}</p>
            <p>击杀数: ${summary.players[2].unitsKilled}</p>
            <p>技能使用: ${summary.players[2].skillsUsed}</p>
            <p>得分: ${summary.players[2].score}</p>
        `;
        
        modal.classList.remove('hidden');
    }

    toggleAI() {
        this.aiEnabled = !this.aiEnabled;
        const btn = document.getElementById('btn-ai-toggle');
        btn.textContent = this.aiEnabled ? '关闭AI' : 'AI对战';
        this.addLog(this.aiEnabled ? 'AI对战已开启' : 'AI对战已关闭', 'log-skill');
    }

    saveGame() {
        const gameState = {
            turn: this.turnManager.currentTurn,
            currentPlayer: this.turnManager.currentPlayer,
            units: this.units,
            map: this.mapData,
            moveHistory: this.turnManager.moveHistory,
            turnHistory: this.turnManager.turnHistory,
            replayData: this.replayFrames
        };
        
        const result = this.gameStorage.saveGame(gameState);
        this.addLog(result.message, 'log-skill');
    }

    loadGame() {
        const result = this.gameStorage.loadGame();
        
        if (result.success) {
            const state = result.gameState;
            this.units = state.units;
            this.mapData = state.map;
            this.turnManager.currentTurn = state.turn;
            this.turnManager.currentPlayer = state.currentPlayer;
            this.turnManager.moveHistory = state.moveHistory;
            this.turnManager.turnHistory = state.turnHistory;
            this.replayFrames = state.replayData || [];
            
            if (this.mapData && this.mapData.length > 0) {
                this.canvas.width = this.mapData[0].length * this.tileSize;
                this.canvas.height = this.mapData.length * this.tileSize;
            }
            
            this.selectedUnit = null;
            this.reachableTiles = [];
            this.attackableTiles = [];
            this.turnManager.phase = 'select';
            
            this.updateWinProbability();
            this.addLog(`读取存档成功 - 回合 ${state.turn}`, 'log-skill');
            this.updateUI();
            this.render();
        } else {
            this.addLog(result.message, 'log-damage');
        }
    }

    undoAction() {
        const result = this.turnManager.undoLastAction(this.units, this.mapGenerator);
        
        if (result.success) {
            if (this.selectedUnit) {
                this.updateReachableTiles();
            }
            
            this.updateWinProbability();
            this.addLog('已撤销上一步操作', 'log-skill');
            this.updateUI();
            this.render();
        } else {
            this.addLog(result.reason, 'log-damage');
        }
    }

    updateWinProbability() {
        this.winProbability = this.gamePredictor.calculateWinProbability(this.units);
    }

    captureFrame(highlights, logEntry) {
        const frame = {
            turn: this.turnManager.currentTurn,
            currentPlayer: this.turnManager.currentPlayer,
            map: this.mapData.map(row => row.map(tile => ({
                id: tile.id,
                color: tile.color
            }))),
            units: this.units.map(unit => ({
                id: unit.id,
                name: unit.name,
                player: unit.player,
                x: unit.x,
                y: unit.y,
                hp: unit.hp,
                maxHp: unit.maxHp,
                color: unit.color
            })),
            highlights: highlights,
            logEntry: logEntry
        };
        
        this.replayFrames.push(frame);
    }

    saveReplay() {
        const replayData = {
            mapWidth: GameConfig.mapWidth,
            mapHeight: GameConfig.mapHeight,
            frames: this.replayFrames
        };
        
        this.gameStorage.saveReplay(replayData);
    }

    showReplayModal() {
        const replays = this.gameStorage.getAllReplays();
        
        if (replays.length === 0) {
            alert('没有可回放的战局');
            return;
        }
        
        if (this.replayManager) {
            this.replayManager.destroy();
            this.replayManager = null;
        }
        
        const latestReplay = replays[0];
        const replayCanvas = document.getElementById('replay-canvas');
        this.replayManager = new ReplayManager(replayCanvas);
        this.replayManager.loadReplay(latestReplay.data);
        
        this.replayManager.onFrameUpdate = (frameIndex, frame) => {
            document.getElementById('replay-turn-info').textContent = 
                `帧: ${frameIndex + 1}/${this.replayManager.getTotalFrames()}`;
        };
        
        this.replayManager.onReplayEnd = () => {
            document.getElementById('btn-replay-play').textContent = '播放';
        };
        
        document.getElementById('replay-modal').classList.remove('hidden');
    }

    playReplay() {
        if (this.replayManager) {
            this.replayManager.play();
            document.getElementById('btn-replay-play').textContent = '播放中...';
        }
    }

    pauseReplay() {
        if (this.replayManager) {
            this.replayManager.pause();
            document.getElementById('btn-replay-play').textContent = '播放';
        }
    }

    changeReplaySpeed() {
        if (!this.replayManager) return;
        
        const speeds = [1, 2, 4, 0.5];
        const currentSpeed = this.replayManager.playbackSpeed;
        const currentIndex = speeds.indexOf(currentSpeed);
        const nextSpeed = speeds[(currentIndex + 1) % speeds.length];
        
        this.replayManager.setSpeed(nextSpeed);
        document.getElementById('btn-replay-speed').textContent = `速度: ${nextSpeed}x`;
    }

    closeReplay() {
        if (this.replayManager) {
            this.replayManager.destroy();
            this.replayManager = null;
        }
        
        document.getElementById('replay-modal').classList.add('hidden');
    }

    showMultiplayerModal() {
        document.getElementById('multiplayer-modal').classList.remove('hidden');
    }

    hideMultiplayerModal() {
        document.getElementById('multiplayer-modal').classList.add('hidden');
        this.networkManager.disconnect();
    }

    createRoom() {
        const playerName = document.getElementById('player-name').value || '玩家1';
        this.networkManager.setPlayerName(playerName);
        
        const result = this.networkManager.createRoom();
        
        if (result.success) {
            document.getElementById('connection-status').textContent = 
                `状态: 已创建房间 - ${result.roomCode}`;
            document.getElementById('room-code').value = result.roomCode;
            
            this.addLog(`创建房间: ${result.roomCode}`, 'log-skill');
        }
    }

    joinRoom() {
        const playerName = document.getElementById('player-name').value || '玩家2';
        const roomCode = document.getElementById('room-code').value;
        
        this.networkManager.setPlayerName(playerName);
        
        const result = this.networkManager.joinRoom(roomCode);
        
        if (result.success) {
            document.getElementById('connection-status').textContent = 
                `状态: 已加入房间 - ${result.roomCode}`;
            
            this.addLog(`加入房间: ${result.roomCode}`, 'log-skill');
        } else {
            document.getElementById('connection-status').textContent = 
                `状态: ${result.message}`;
        }
    }

    showStatsModal() {
        const globalStats = this.statisticsManager.getGlobalStats();
        const rankings = this.statisticsManager.getRankings();
        const unitStats = this.statisticsManager.getAllUnitStatistics();
        
        document.getElementById('global-stats').innerHTML = `
            <p>总游戏数: ${globalStats.totalGames}</p>
            <p>红方胜场: ${globalStats.totalWins[1]}</p>
            <p>蓝方胜场: ${globalStats.totalWins[2]}</p>
            <p>平均游戏时长: ${Math.round(globalStats.averageGameLength)}秒</p>
        `;
        
        let rankingsHTML = '';
        for (const rank of rankings) {
            rankingsHTML += `
                <div class="ranking-item">
                    <span class="rank-player ${rank.won ? 'winner' : ''}">${rank.name}</span>
                    <span>得分: ${rank.score}</span>
                    <span>伤害: ${rank.totalDamage}</span>
                </div>
            `;
        }
        document.getElementById('rankings-list').innerHTML = rankingsHTML || '<p>暂无排行</p>';
        
        let unitStatsHTML = '';
        for (const [type, stats] of Object.entries(unitStats)) {
            const winRate = stats.gamesPlayed > 0 ? 
                Math.round((stats.wins / stats.gamesPlayed) * 100) : 0;
            unitStatsHTML += `
                <div class="unit-stat-item">
                    <span>${this.getTypeName(type)}</span>
                    <span>出场: ${stats.gamesPlayed}次</span>
                    <span>胜率: ${winRate}%</span>
                    <span>总伤害: ${stats.totalDamage}</span>
                </div>
            `;
        }
        document.getElementById('unit-stats-list').innerHTML = unitStatsHTML || '<p>暂无统计</p>';
        
        document.getElementById('stats-modal').classList.remove('hidden');
    }

    hideStatsModal() {
        document.getElementById('stats-modal').classList.add('hidden');
    }

    addLog(message, type = '') {
        const logContent = document.getElementById('log-content');
        const entry = document.createElement('div');
        entry.className = `log-entry ${type}`;
        entry.textContent = message;
        logContent.appendChild(entry);
        logContent.scrollTop = logContent.scrollHeight;
        
        this.statisticsManager.recordBattleLog({ message, type });
    }

    updateUI() {
        document.getElementById('turn-info').textContent = `回合: ${this.turnManager.currentTurn}`;
        document.getElementById('current-player').textContent = 
            `当前玩家: ${this.turnManager.currentPlayer === 1 ? '红方' : '蓝方'}`;
        document.getElementById('current-player').className = 
            this.turnManager.currentPlayer === 1 ? 'player1' : 'player2';
        
        let statusText = '准备中...';
        if (this.turnManager.phase === 'select') {
            statusText = '选择棋子';
        } else if (this.turnManager.phase === 'action') {
            statusText = '选择行动';
        } else if (this.turnManager.phase === 'game_over') {
            statusText = '游戏结束';
        }
        document.getElementById('game-status').textContent = statusText;
        
        this.updateWinProbabilityDisplay();
        this.updateUnitInfo();
        this.updateSkillButtons();
    }

    updateWinProbabilityDisplay() {
        const display = document.getElementById('win-probability');
        if (display) {
            display.innerHTML = `
                <span class="prob-player1" style="width: ${this.winProbability.player1}%"></span>
                <span class="prob-text">红方 ${this.winProbability.player1}% : ${this.winProbability.player2}% 蓝方</span>
            `;
        }
    }

    updateUnitInfo() {
        const unit = this.turnManager.selectedUnit;
        
        if (unit) {
            document.getElementById('unit-name').textContent = unit.name;
            document.getElementById('unit-name').style.color = unit.rarity ? unit.rarity.color : '#fff';
            document.getElementById('unit-type').textContent = this.getTypeName(unit.type);
            document.getElementById('unit-level').textContent = unit.level;
            
            const rarityText = unit.rarity ? ` (${unit.rarity.name})` : '';
            document.getElementById('unit-rarity').textContent = rarityText;
            
            document.getElementById('unit-hp').textContent = `${unit.hp}/${unit.maxHp}`;
            document.getElementById('unit-atk').textContent = unit.getEffectiveAtk();
            document.getElementById('unit-def').textContent = unit.getEffectiveDef();
            document.getElementById('unit-mov').textContent = unit.move;
            document.getElementById('unit-rng').textContent = unit.range;
            
            if (unit.traits && unit.traits.length > 0) {
                const traitsHTML = unit.traits.map(t => 
                    `<span class="trait" title="${t.effect}">${t.name}</span>`
                ).join(' ');
                document.getElementById('unit-traits').innerHTML = traitsHTML;
            } else {
                document.getElementById('unit-traits').innerHTML = '';
            }
        } else {
            ['unit-name', 'unit-type', 'unit-level', 'unit-hp', 'unit-atk', 'unit-def', 'unit-mov', 'unit-rng', 'unit-rarity']
                .forEach(id => {
                    const el = document.getElementById(id);
                    if (el) el.textContent = '-';
                });
            document.getElementById('unit-traits').innerHTML = '';
        }
    }

    getTypeName(type) {
        const names = {
            [UnitTypes.WARRIOR]: '战士',
            [UnitTypes.ARCHER]: '弓箭手',
            [UnitTypes.MAGE]: '法师',
            [UnitTypes.KNIGHT]: '骑士',
            [UnitTypes.HEALER]: '治疗师',
            [UnitTypes.ASSASSIN]: '刺客'
        };
        return names[type] || type;
    }

    updateSkillButtons() {
        const skillButtons = document.getElementById('skill-buttons');
        skillButtons.innerHTML = '';
        
        const unit = this.turnManager.selectedUnit;
        
        if (!unit) return;
        
        const moveBtn = document.createElement('button');
        moveBtn.className = 'skill-btn';
        moveBtn.textContent = '移动';
        moveBtn.disabled = unit.hasMoved;
        moveBtn.onclick = () => {
            this.turnManager.setMoveMode();
            this.updateUI();
            this.render();
        };
        skillButtons.appendChild(moveBtn);
        
        const attackBtn = document.createElement('button');
        attackBtn.className = 'skill-btn';
        attackBtn.textContent = '攻击';
        attackBtn.disabled = unit.hasActed;
        attackBtn.onclick = () => {
            this.turnManager.setAttackMode();
            this.updateUI();
            this.render();
        };
        skillButtons.appendChild(attackBtn);
        
        for (const skill of unit.skills) {
            const btn = document.createElement('button');
            btn.className = 'skill-btn';
            btn.textContent = `${skill.name}${skill.currentCooldown > 0 ? ` (${skill.currentCooldown})` : ''}`;
            btn.disabled = unit.hasActed || skill.currentCooldown > 0;
            btn.onclick = () => this.useSkill(skill.id);
            
            btn.title = skill.description;
            
            skillButtons.appendChild(btn);
        }
    }

    render() {
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        
        this.renderMap();
        this.renderHighlights();
        this.renderUnits();
        this.renderHover();
        this.renderWinProbabilityBar();
        
        this.renderUI();
    }

    renderMap() {
        for (let y = 0; y < this.mapData.length; y++) {
            for (let x = 0; x < this.mapData[y].length; x++) {
                const tile = this.mapData[y][x];
                
                this.ctx.fillStyle = tile.color;
                this.ctx.fillRect(
                    x * this.tileSize,
                    y * this.tileSize,
                    this.tileSize,
                    this.tileSize
                );
                
                this.ctx.strokeStyle = 'rgba(0, 0, 0, 0.3)';
                this.ctx.lineWidth = 1;
                this.ctx.strokeRect(
                    x * this.tileSize,
                    y * this.tileSize,
                    this.tileSize,
                    this.tileSize
                );
            }
        }
    }

    renderHighlights() {
        if (this.turnManager.actionMode === 'move' && this.reachableTiles.length > 0) {
            this.ctx.fillStyle = 'rgba(100, 200, 255, 0.3)';
            for (const tile of this.reachableTiles) {
                this.ctx.fillRect(
                    tile.x * this.tileSize,
                    tile.y * this.tileSize,
                    this.tileSize,
                    this.tileSize
                );
            }
        }
        
        if ((this.turnManager.actionMode === 'attack' || this.turnManager.actionMode === 'skill') 
            && this.attackableTiles.length > 0) {
            this.ctx.fillStyle = 'rgba(255, 100, 100, 0.3)';
            for (const tile of this.attackableTiles) {
                this.ctx.fillRect(
                    tile.x * this.tileSize,
                    tile.y * this.tileSize,
                    this.tileSize,
                    this.tileSize
                );
            }
        }
        
        if (this.turnManager.selectedUnit) {
            const unit = this.turnManager.selectedUnit;
            this.ctx.strokeStyle = '#ffd700';
            this.ctx.lineWidth = 3;
            this.ctx.strokeRect(
                unit.x * this.tileSize,
                unit.y * this.tileSize,
                this.tileSize,
                this.tileSize
            );
        }
    }

    renderUnits() {
        for (const unit of this.units) {
            if (unit.hp <= 0) continue;
            
            const centerX = unit.x * this.tileSize + this.tileSize / 2;
            const centerY = unit.y * this.tileSize + this.tileSize / 2;
            
            this.ctx.beginPath();
            this.ctx.arc(centerX, centerY, this.tileSize * 0.35, 0, Math.PI * 2);
            this.ctx.fillStyle = unit.rarity ? unit.rarity.color : unit.color;
            this.ctx.fill();
            
            this.ctx.strokeStyle = unit.player === 1 ? '#ff6b6b' : '#4ecdc4';
            this.ctx.lineWidth = 3;
            this.ctx.stroke();
            
            const hpPercent = unit.hp / unit.maxHp;
            const barWidth = this.tileSize * 0.6;
            const barHeight = 6;
            const barX = centerX - barWidth / 2;
            const barY = centerY + this.tileSize * 0.3;
            
            this.ctx.fillStyle = '#333';
            this.ctx.fillRect(barX, barY, barWidth, barHeight);
            
            this.ctx.fillStyle = hpPercent > 0.5 ? '#4ecdc4' : hpPercent > 0.25 ? '#fbbf24' : '#ff6b6b';
            this.ctx.fillRect(barX, barY, barWidth * hpPercent, barHeight);
            
            this.ctx.strokeStyle = '#000';
            this.ctx.lineWidth = 1;
            this.ctx.strokeRect(barX, barY, barWidth, barHeight);
            
            this.ctx.fillStyle = '#fff';
            this.ctx.font = 'bold 12px Arial';
            this.ctx.textAlign = 'center';
            this.ctx.textBaseline = 'middle';
            this.ctx.fillText(unit.name.charAt(0), centerX, centerY);
            
            if (unit.hasActed) {
                this.ctx.fillStyle = 'rgba(0, 0, 0, 0.5)';
                this.ctx.beginPath();
                this.ctx.arc(centerX, centerY, this.tileSize * 0.35, 0, Math.PI * 2);
                this.ctx.fill();
            }
        }
    }

    renderHover() {
        if (!this.hoveredTile) return;
        
        const { x, y } = this.hoveredTile;
        
        this.ctx.strokeStyle = 'rgba(255, 255, 255, 0.5)';
        this.ctx.lineWidth = 2;
        this.ctx.strokeRect(
            x * this.tileSize,
            y * this.tileSize,
            this.tileSize,
            this.tileSize
        );
        
        const hoveredUnit = this.getUnitAt(x, y);
        if (hoveredUnit) {
            this.ctx.fillStyle = 'rgba(0, 0, 0, 0.85)';
            const tooltipWidth = 120;
            const tooltipHeight = 60;
            const tooltipX = x * this.tileSize + this.tileSize;
            const tooltipY = y * this.tileSize;
            
            this.ctx.fillRect(tooltipX, tooltipY, tooltipWidth, tooltipHeight);
            
            this.ctx.fillStyle = hoveredUnit.rarity ? hoveredUnit.rarity.color : '#fff';
            this.ctx.font = 'bold 11px Arial';
            this.ctx.textAlign = 'left';
            this.ctx.fillText(hoveredUnit.name, tooltipX + 5, tooltipY + 15);
            
            this.ctx.fillStyle = '#fff';
            this.ctx.font = '10px Arial';
            this.ctx.fillText(`HP: ${hoveredUnit.hp}/${hoveredUnit.maxHp}`, tooltipX + 5, tooltipY + 30);
            this.ctx.fillText(`ATK: ${hoveredUnit.getEffectiveAtk()} DEF: ${hoveredUnit.getEffectiveDef()}`, tooltipX + 5, tooltipY + 45);
        }
        
        const tile = this.mapData[y]?.[x];
        if (tile && !hoveredUnit) {
            this.ctx.fillStyle = 'rgba(0, 0, 0, 0.7)';
            this.ctx.fillRect(x * this.tileSize, y * this.tileSize + this.tileSize - 20, this.tileSize, 20);
            
            this.ctx.fillStyle = '#fff';
            this.ctx.font = '10px Arial';
            this.ctx.textAlign = 'center';
            this.ctx.textBaseline = 'middle';
            this.ctx.fillText(tile.name, x * this.tileSize + this.tileSize / 2, y * this.tileSize + this.tileSize - 10);
        }
    }

    renderWinProbabilityBar() {
        const barWidth = this.canvas.width;
        const barHeight = 8;
        const barY = this.canvas.height - barHeight;
        
        this.ctx.fillStyle = '#333';
        this.ctx.fillRect(0, barY, barWidth, barHeight);
        
        const player1Width = (this.winProbability.player1 / 100) * barWidth;
        
        this.ctx.fillStyle = '#ff6b6b';
        this.ctx.fillRect(0, barY, player1Width, barHeight);
        
        this.ctx.fillStyle = '#4ecdc4';
        this.ctx.fillRect(player1Width, barY, barWidth - player1Width, barHeight);
        
        this.ctx.strokeStyle = '#000';
        this.ctx.lineWidth = 1;
        this.ctx.strokeRect(0, barY, barWidth, barHeight);
    }

    renderUI() {
    }

    update() {
        this.render();
    }
}

window.addEventListener('DOMContentLoaded', () => {
    window.game = new Game();
});
