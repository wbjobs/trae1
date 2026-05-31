class ReplayManager {
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.replayData = null;
        this.currentFrame = 0;
        this.isPlaying = false;
        this.playbackSpeed = 1;
        this.animationFrame = null;
        this.lastFrameTime = 0;
        this.frameDelay = 500;
        
        this.onFrameUpdate = null;
        this.onReplayEnd = null;
        
        this.tileSize = 50;
        this.mapWidth = 15;
        this.mapHeight = 12;
        
        this.adjustCanvasSize();
    }

    adjustCanvasSize() {
        if (this.replayData && this.replayData.mapWidth) {
            this.mapWidth = this.replayData.mapWidth;
            this.mapHeight = this.replayData.mapHeight;
        }
        
        this.canvas.width = this.mapWidth * this.tileSize;
        this.canvas.height = this.mapHeight * this.tileSize;
    }

    loadReplay(replayData) {
        this.replayData = replayData;
        this.currentFrame = 0;
        this.isPlaying = false;
        this.stop();
        
        if (replayData && replayData.frames && replayData.frames.length > 0) {
            this.mapWidth = replayData.mapWidth || 15;
            this.mapHeight = replayData.mapHeight || 12;
            this.adjustCanvasSize();
            this.renderFrame(0);
            return true;
        }
        
        return false;
    }

    play() {
        if (!this.replayData || this.isPlaying) {
            return;
        }
        
        this.isPlaying = true;
        this.lastFrameTime = performance.now();
        this.animate();
    }

    pause() {
        this.isPlaying = false;
        if (this.animationFrame) {
            cancelAnimationFrame(this.animationFrame);
            this.animationFrame = null;
        }
    }

    stop() {
        this.pause();
        this.currentFrame = 0;
    }

    seek(frame) {
        if (!this.replayData) {
            return;
        }
        
        this.currentFrame = Math.max(0, Math.min(frame, this.replayData.frames.length - 1));
        this.renderFrame(this.currentFrame);
        
        if (this.onFrameUpdate) {
            this.onFrameUpdate(this.currentFrame, this.replayData.frames[this.currentFrame]);
        }
    }

    nextFrame() {
        if (!this.replayData) {
            return;
        }
        
        if (this.currentFrame < this.replayData.frames.length - 1) {
            this.currentFrame++;
            this.renderFrame(this.currentFrame);
            
            if (this.onFrameUpdate) {
                this.onFrameUpdate(this.currentFrame, this.replayData.frames[this.currentFrame]);
            }
        } else {
            this.pause();
            if (this.onReplayEnd) {
                this.onReplayEnd();
            }
        }
    }

    prevFrame() {
        if (!this.replayData) {
            return;
        }
        
        if (this.currentFrame > 0) {
            this.currentFrame--;
            this.renderFrame(this.currentFrame);
            
            if (this.onFrameUpdate) {
                this.onFrameUpdate(this.currentFrame, this.replayData.frames[this.currentFrame]);
            }
        }
    }

    setSpeed(speed) {
        this.playbackSpeed = speed;
        this.frameDelay = 500 / speed;
    }

    animate() {
        if (!this.isPlaying) {
            return;
        }
        
        const now = performance.now();
        const elapsed = now - this.lastFrameTime;
        
        if (elapsed >= this.frameDelay) {
            this.nextFrame();
            this.lastFrameTime = now;
        }
        
        this.animationFrame = requestAnimationFrame(() => this.animate());
    }

    renderFrame(frameIndex) {
        if (!this.replayData || !this.replayData.frames[frameIndex]) {
            return;
        }
        
        const frame = this.replayData.frames[frameIndex];
        
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        
        if (frame.map) {
            this.renderMap(frame.map);
        }
        
        if (frame.units) {
            this.renderUnits(frame.units);
        }
        
        if (frame.highlights) {
            this.renderHighlights(frame.highlights);
        }
        
        if (frame.logEntry) {
            this.renderLogEntry(frame.logEntry);
        }
    }

    renderMap(map) {
        for (let y = 0; y < map.length; y++) {
            for (let x = 0; x < map[y].length; x++) {
                const tile = map[y][x];
                
                this.ctx.fillStyle = tile.color || '#90EE90';
                this.ctx.fillRect(
                    x * this.tileSize,
                    y * this.tileSize,
                    this.tileSize,
                    this.tileSize
                );
                
                this.ctx.strokeStyle = 'rgba(0, 0, 0, 0.2)';
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

    renderUnits(units) {
        const sortedUnits = [...units].sort((a, b) => {
            if (a.y !== b.y) return a.y - b.y;
            return a.x - b.x;
        });
        
        for (const unit of sortedUnits) {
            if (unit.hp <= 0) continue;
            
            const centerX = unit.x * this.tileSize + this.tileSize / 2;
            const centerY = unit.y * this.tileSize + this.tileSize / 2;
            
            this.ctx.beginPath();
            this.ctx.arc(centerX, centerY, this.tileSize * 0.35, 0, Math.PI * 2);
            this.ctx.fillStyle = unit.color || '#ffffff';
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
            this.ctx.fillText(unit.name ? unit.name.charAt(0) : '?', centerX, centerY);
        }
    }

    renderHighlights(highlights) {
        if (!highlights) return;
        
        if (highlights.reachable) {
            this.ctx.fillStyle = 'rgba(100, 200, 255, 0.3)';
            for (const tile of highlights.reachable) {
                this.ctx.fillRect(
                    tile.x * this.tileSize,
                    tile.y * this.tileSize,
                    this.tileSize,
                    this.tileSize
                );
            }
        }
        
        if (highlights.attackable) {
            this.ctx.fillStyle = 'rgba(255, 100, 100, 0.3)';
            for (const tile of highlights.attackable) {
                this.ctx.fillRect(
                    tile.x * this.tileSize,
                    tile.y * this.tileSize,
                    this.tileSize,
                    this.tileSize
                );
            }
        }
        
        if (highlights.selected) {
            this.ctx.strokeStyle = '#ffd700';
            this.ctx.lineWidth = 3;
            this.ctx.strokeRect(
                highlights.selected.x * this.tileSize,
                highlights.selected.y * this.tileSize,
                this.tileSize,
                this.tileSize
            );
        }
    }

    renderLogEntry(logEntry) {
    }

    getTotalFrames() {
        return this.replayData ? this.replayData.frames.length : 0;
    }

    getCurrentFrameData() {
        if (!this.replayData || !this.replayData.frames[this.currentFrame]) {
            return null;
        }
        return this.replayData.frames[this.currentFrame];
    }

    createReplayFrame(gameState, highlights = null, logEntry = null) {
        return {
            turn: gameState.turn,
            currentPlayer: gameState.currentPlayer,
            map: gameState.map.map(row => row.map(tile => ({
                id: tile.id,
                color: tile.color
            }))),
            units: gameState.units.map(unit => ({
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
    }

    exportReplay() {
        if (!this.replayData) {
            return null;
        }
        
        return btoa(JSON.stringify(this.replayData));
    }

    importReplay(encodedData) {
        try {
            const replayData = JSON.parse(atob(encodedData));
            return this.loadReplay(replayData);
        } catch (error) {
            console.error('导入回放失败:', error);
            return false;
        }
    }

    destroy() {
        this.stop();
        this.replayData = null;
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    }
}
