class NetworkManager {
    constructor() {
        this.isConnected = false;
        this.isHost = false;
        this.roomCode = null;
        this.playerName = '玩家';
        this.playerId = null;
        this.onMessage = null;
        this.onPlayerJoin = null;
        this.onPlayerLeave = null;
        this.onGameStart = null;
        this.onGameAction = null;
        this.onGameStateUpdate = null;
        this.simulatedDelay = 100;
        
        this.rooms = new Map();
        this.players = new Map();
        this.messageQueue = [];
        
        this.broadcastChannel = null;
        this.storageEventHandler = null;
        
        this.initBroadcastChannel();
    }

    initBroadcastChannel() {
        if ('BroadcastChannel' in window) {
            try {
                this.broadcastChannel = new BroadcastChannel('strategy_chess_channel');
                this.broadcastChannel.onmessage = (event) => {
                    this.handleBroadcastMessage(event.data);
                };
            } catch (e) {
                console.log('BroadcastChannel not available, using localStorage fallback');
            }
        }
        
        this.storageEventHandler = (event) => {
            if (event.key === 'strategy_chess_message' && event.newValue) {
                try {
                    const message = JSON.parse(event.newValue);
                    this.handleBroadcastMessage(message);
                } catch (e) {
                    console.error('Parse message error:', e);
                }
            }
        };
        window.addEventListener('storage', this.storageEventHandler);
    }

    handleBroadcastMessage(message) {
        if (!message || message.roomCode !== this.roomCode) {
            return;
        }
        
        if (message.type === 'player_join' && message.playerId !== this.playerId) {
            if (this.onPlayerJoin) {
                this.onPlayerJoin({
                    id: message.playerId,
                    name: message.playerName
                });
            }
        } else if (message.type === 'player_leave' && message.playerId !== this.playerId) {
            if (this.onPlayerLeave) {
                this.onPlayerLeave({
                    id: message.playerId,
                    name: message.playerName
                });
            }
        } else if (message.type === 'game_start' && message.playerId !== this.playerId) {
            if (this.onGameStart) {
                this.onGameStart(message.gameState);
            }
        } else if (message.type === 'game_action' && message.playerId !== this.playerId) {
            if (this.onGameAction) {
                this.onGameAction(message);
            }
        } else if (message.type === 'game_state_update' && message.playerId !== this.playerId) {
            if (this.onGameStateUpdate) {
                this.onGameStateUpdate(message.gameState);
            }
        } else if (message.type === 'chat' && message.playerId !== this.playerId) {
            if (this.onMessage) {
                this.onMessage(message);
            }
        }
    }

    broadcastMessage(message) {
        const fullMessage = {
            ...message,
            roomCode: this.roomCode
        };
        
        if (this.broadcastChannel) {
            this.broadcastChannel.postMessage(fullMessage);
        }
        
        try {
            localStorage.setItem('strategy_chess_message', JSON.stringify(fullMessage));
            localStorage.removeItem('strategy_chess_message');
        } catch (e) {
            console.error('Broadcast message error:', e);
        }
    }

    setPlayerName(name) {
        this.playerName = name;
    }

    async createRoom() {
        this.isHost = true;
        this.roomCode = this.generateRoomCode();
        this.playerId = 'player1';
        
        this.rooms.set(this.roomCode, {
            host: this.playerId,
            players: [{
                id: this.playerId,
                name: this.playerName,
                color: '#ff6b6b'
            }],
            gameState: null,
            isStarted: false
        });
        
        this.isConnected = true;
        
        return {
            success: true,
            roomCode: this.roomCode,
            playerId: this.playerId
        };
    }

    async joinRoom(roomCode) {
        const room = this.rooms.get(roomCode);
        
        if (!room) {
            return { success: false, message: '房间不存在' };
        }
        
        if (room.players.length >= 2) {
            return { success: false, message: '房间已满' };
        }
        
        if (room.isStarted) {
            return { success: false, message: '游戏已开始' };
        }
        
        this.isHost = false;
        this.roomCode = roomCode;
        this.playerId = 'player2';
        
        room.players.push({
            id: this.playerId,
            name: this.playerName,
            color: '#4ecdc4'
        });
        
        this.isConnected = true;
        
        this.broadcastMessage({
            type: 'player_join',
            playerId: this.playerId,
            playerName: this.playerName
        });
        
        return {
            success: true,
            roomCode: this.roomCode,
            playerId: this.playerId,
            players: room.players
        };
    }

    startGame(gameState) {
        const room = this.rooms.get(this.roomCode);
        
        if (!room) {
            return { success: false, message: '房间不存在' };
        }
        
        room.gameState = gameState;
        room.isStarted = true;
        
        this.broadcastMessage({
            type: 'game_start',
            playerId: this.playerId,
            gameState: gameState
        });
        
        return { success: true };
    }

    sendAction(action) {
        if (!this.isConnected || !this.roomCode) {
            return;
        }
        
        const message = {
            type: 'game_action',
            playerId: this.playerId,
            action: action,
            timestamp: Date.now()
        };
        
        this.broadcastMessage(message);
    }

    sendGameStateUpdate(gameState) {
        if (!this.isConnected || !this.roomCode) {
            return;
        }
        
        const message = {
            type: 'game_state_update',
            playerId: this.playerId,
            gameState: gameState,
            timestamp: Date.now()
        };
        
        this.broadcastMessage(message);
    }

    sendTurnEnd(turnData) {
        if (!this.isConnected || !this.roomCode) {
            return;
        }
        
        const message = {
            type: 'turn_end',
            playerId: this.playerId,
            turnData: turnData,
            timestamp: Date.now()
        };
        
        this.broadcastMessage(message);
    }

    sendChat(message) {
        if (!this.isConnected || !this.roomCode) {
            return;
        }
        
        const chatMessage = {
            type: 'chat',
            playerId: this.playerId,
            playerName: this.playerName,
            message: message,
            timestamp: Date.now()
        };
        
        this.broadcastMessage(chatMessage);
    }

    leaveRoom() {
        if (this.roomCode && this.rooms.has(this.roomCode)) {
            const room = this.rooms.get(this.roomCode);
            
            this.broadcastMessage({
                type: 'player_leave',
                playerId: this.playerId,
                playerName: this.playerName
            });
            
            room.players = room.players.filter(p => p.id !== this.playerId);
            
            if (room.players.length === 0) {
                this.rooms.delete(this.roomCode);
            }
        }
        
        this.isConnected = false;
        this.isHost = false;
        this.roomCode = null;
        this.playerId = null;
    }

    getRoomInfo() {
        if (!this.roomCode || !this.rooms.has(this.roomCode)) {
            return null;
        }
        
        const room = this.rooms.get(this.roomCode);
        return {
            roomCode: this.roomCode,
            isHost: this.isHost,
            isStarted: room.isStarted,
            players: room.players
        };
    }

    getAvailableRooms() {
        return Array.from(this.rooms.entries())
            .filter(([code, room]) => !room.isStarted && room.players.length < 2)
            .map(([code, room]) => ({
                code,
                players: room.players.length,
                hostName: room.players[0]?.name || '未知'
            }));
    }

    generateRoomCode() {
        const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
        let code = '';
        for (let i = 0; i < 6; i++) {
            code += chars.charAt(Math.floor(Math.random() * chars.length));
        }
        return code;
    }

    simulateOpponentAction(action) {
        setTimeout(() => {
            if (this.onGameAction) {
                this.onGameAction({
                    type: 'action',
                    playerId: 'player2',
                    action: action,
                    timestamp: Date.now()
                });
            }
        }, this.simulatedDelay);
    }

    disconnect() {
        this.leaveRoom();
        
        if (this.broadcastChannel) {
            this.broadcastChannel.close();
            this.broadcastChannel = null;
        }
        
        if (this.storageEventHandler) {
            window.removeEventListener('storage', this.storageEventHandler);
            this.storageEventHandler = null;
        }
    }
}
