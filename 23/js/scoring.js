class ScoreManager {
    constructor() {
        this.score = 0;
        this.baseScore = 1000;
        this.timeBonus = 0;
        this.speedBonus = 0;
        this.collisionPenalty = 50;
        this.checkpointBonus = 50;
        this.lapBonus = 200;
        
        this.startTime = 0;
        this.elapsedTime = 0;
        this.maxSpeed = 0;
        this.collisionCount = 0;
        this.lap = 0;
        this.maxLaps = 3;
        this.totalLapTimes = [];
        this.currentLapStartTime = 0;
        
        this.leaderboard = [];
        this.loadLeaderboard();
        
        this.gameState = 'waiting';
        
        this.uiUpdateInterval = 0.1;
        this.lastUIUpdate = 0;
        
        this.displayScore = 0;
        this.displaySpeed = 0;
        this.displayTime = '00:00.00';
    }

    startGame() {
        this.score = 0;
        this.displayScore = 0;
        this.startTime = Date.now();
        this.elapsedTime = 0;
        this.maxSpeed = 0;
        this.displaySpeed = 0;
        this.collisionCount = 0;
        this.lap = 0;
        this.totalLapTimes = [];
        this.currentLapStartTime = Date.now();
        this.gameState = 'playing';
        this.lastUIUpdate = 0;
        
        this.updateUI(true);
    }

    update(currentSpeed, collisionCount, currentTime) {
        if (this.gameState !== 'playing') return;
        
        currentTime = currentTime || Date.now();
        
        this.elapsedTime = currentTime - this.startTime;
        this.collisionCount = collisionCount;
        
        if (currentSpeed > this.maxSpeed) {
            this.maxSpeed = currentSpeed;
        }
        
        this.calculateScore();
        
        if ((currentTime / 1000) - this.lastUIUpdate > this.uiUpdateInterval) {
            this.updateUI(false);
            this.lastUIUpdate = currentTime / 1000;
        }
    }

    calculateScore() {
        const timeSeconds = this.elapsedTime / 1000;
        
        this.score = this.baseScore;
        
        if (timeSeconds > 0) {
            this.timeBonus = Math.max(0, 500 - timeSeconds * 2);
            this.score += this.timeBonus;
        }
        
        this.speedBonus = this.maxSpeed * 2;
        this.score += this.speedBonus;
        
        this.score -= this.collisionCount * this.collisionPenalty;
        
        this.score = Math.max(0, Math.floor(this.score));
    }

    addCheckpointScore() {
        this.score += this.checkpointBonus;
        this.displayScore = this.score;
        this.showFloatingScore(`+${this.checkpointBonus} 检查点!`);
        this.updateUI(true);
    }

    addLapScore() {
        this.lap++;
        const lapTime = Date.now() - this.currentLapStartTime;
        this.totalLapTimes.push(lapTime);
        this.currentLapStartTime = Date.now();
        
        if (this.lap <= this.maxLaps) {
            this.score += this.lapBonus;
            this.displayScore = this.score;
            this.showFloatingScore(`+${this.lapBonus} 第${this.lap}圈完成!`);
        }
        
        this.updateUI(true);
        
        if (this.lap >= this.maxLaps) {
            this.finishGame();
        }
    }

    finishGame() {
        this.gameState = 'finished';
        this.saveToLeaderboard();
        this.showFinalResults();
    }

    getLap() {
        return Math.min(this.lap, this.maxLaps);
    }

    getMaxLaps() {
        return this.maxLaps;
    }

    isFinished() {
        return this.gameState === 'finished';
    }

    getFormattedTime() {
        return this.formatTime(this.elapsedTime);
    }

    getCurrentLapTime() {
        const currentLapTime = Date.now() - this.currentLapStartTime;
        return this.formatTime(currentLapTime);
    }

    formatTime(milliseconds) {
        const totalSeconds = Math.floor(milliseconds / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        const centiseconds = Math.floor((milliseconds % 1000) / 10);
        
        return `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}.${centiseconds.toString().padStart(2, '0')}`;
    }

    updateUI(forceUpdate) {
        this.displayScore = this.score;
        this.displaySpeed = Math.floor(this.maxSpeed * 3.6);
        this.displayTime = this.getFormattedTime();
        
        const scoreElement = document.getElementById('score-value');
        const timeElement = document.getElementById('time-value');
        const lapElement = document.getElementById('lap-value');
        const collisionElement = document.getElementById('collision-value');
        const speedElement = document.getElementById('speed-value');
        
        if (scoreElement) scoreElement.textContent = this.displayScore;
        if (timeElement) timeElement.textContent = this.displayTime;
        if (lapElement) lapElement.textContent = `${this.getLap()}/${this.maxLaps}`;
        if (collisionElement) collisionElement.textContent = this.collisionCount;
        
        if (speedElement) {
            speedElement.textContent = this.displaySpeed;
            
            if (this.displaySpeed > 100) {
                speedElement.className = 'speed-fast';
            } else if (this.displaySpeed > 50) {
                speedElement.className = 'speed-medium';
            } else {
                speedElement.className = 'speed-slow';
            }
        }
    }

    showFloatingScore(text) {
        const floatText = document.createElement('div');
        floatText.className = 'floating-score';
        floatText.textContent = text;
        floatText.style.cssText = `
            position: absolute;
            top: 40%;
            left: 50%;
            transform: translate(-50%, -50%);
            font-size: 32px;
            font-weight: bold;
            color: #0ff;
            text-shadow: 0 0 20px rgba(0, 255, 255, 0.8);
            pointer-events: none;
            z-index: 50;
            animation: floatUp 1.5s ease-out forwards;
        `;
        
        const uiOverlay = document.getElementById('ui-overlay');
        if (uiOverlay) {
            uiOverlay.appendChild(floatText);
        }
        
        setTimeout(() => {
            if (floatText.parentNode) {
                floatText.parentNode.removeChild(floatText);
            }
        }, 1500);
    }

    showFinalResults() {
        const finalTimeElement = document.getElementById('final-time');
        const finalScoreElement = document.getElementById('final-score');
        const finalCollisionsElement = document.getElementById('final-collisions');
        const finalMaxSpeedElement = document.getElementById('final-max-speed');
        
        if (finalTimeElement) finalTimeElement.textContent = this.getFormattedTime();
        if (finalScoreElement) finalScoreElement.textContent = this.score;
        if (finalCollisionsElement) finalCollisionsElement.textContent = this.collisionCount;
        if (finalMaxSpeedElement) finalMaxSpeedElement.textContent = Math.floor(this.maxSpeed * 3.6);
        
        this.updateLeaderboardUI();
    }

    loadLeaderboard() {
        try {
            const saved = localStorage.getItem('racingLeaderboard');
            if (saved) {
                this.leaderboard = JSON.parse(saved);
            }
        } catch (e) {
            this.leaderboard = [];
        }
    }

    saveToLeaderboard() {
        const entry = {
            score: this.score,
            time: this.elapsedTime,
            formattedTime: this.getFormattedTime(),
            collisions: this.collisionCount,
            maxSpeed: Math.floor(this.maxSpeed * 3.6),
            date: new Date().toLocaleDateString('zh-CN')
        };
        
        this.leaderboard.push(entry);
        this.leaderboard.sort((a, b) => b.score - a.score);
        this.leaderboard = this.leaderboard.slice(0, 10);
        
        try {
            localStorage.setItem('racingLeaderboard', JSON.stringify(this.leaderboard));
        } catch (e) {
            console.log('Could not save leaderboard');
        }
    }

    updateLeaderboardUI() {
        const listElement = document.getElementById('leaderboard-list');
        if (!listElement) return;
        
        listElement.innerHTML = '';
        
        this.leaderboard.slice(0, 5).forEach((entry, index) => {
            const li = document.createElement('li');
            li.innerHTML = `
                <span>${entry.score}分</span>
                <span>${entry.formattedTime}</span>
                <span>${entry.collisions}次碰撞</span>
            `;
            listElement.appendChild(li);
        });
    }

    getGameState() {
        return this.gameState;
    }

    reset() {
        this.gameState = 'waiting';
        this.score = 0;
        this.displayScore = 0;
        this.elapsedTime = 0;
        this.maxSpeed = 0;
        this.displaySpeed = 0;
        this.collisionCount = 0;
        this.lap = 0;
        this.totalLapTimes = [];
        this.lastUIUpdate = 0;
    }
}
