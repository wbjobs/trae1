class Game {
    constructor() {
        this.scene = null;
        this.camera = null;
        this.renderer = null;
        
        this.track = null;
        this.vehicle = null;
        this.collisionDetector = null;
        this.scoreManager = null;
        this.cameraController = null;
        this.replayManager = null;
        
        this.lastVehiclePosition = new THREE.Vector3();
        this.isPaused = false;
        this.isRunning = false;
        this.isReplayMode = false;
        this.lastTime = 0;
        this.currentTime = 0;
        
        this.minimapCanvas = null;
        this.minimapCtx = null;
        this.minimapUpdateInterval = 0.1;
        this.lastMinimapUpdate = 0;
        
        this.keys = {};
        
        this.init();
    }

    init() {
        this.initScene();
        this.initRenderer();
        this.initLighting();
        this.initTrack();
        this.initVehicle();
        this.initCollisionDetector();
        this.initScoreManager();
        this.initCameraController();
        this.initReplayManager();
        this.initMinimap();
        this.initEventListeners();
        this.initUI();
        
        this.animate();
    }

    initScene() {
        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x87ceeb);
        this.scene.fog = new THREE.Fog(0x87ceeb, 100, 500);
        
        this.camera = new THREE.PerspectiveCamera(
            75,
            window.innerWidth / window.innerHeight,
            0.1,
            1000
        );
        this.camera.position.set(0, 10, -15);
        this.camera.lookAt(0, 0, 0);
    }

    initRenderer() {
        const canvas = document.getElementById('game-canvas');
        
        this.renderer = new THREE.WebGLRenderer({
            canvas: canvas,
            antialias: true
        });
        this.renderer.setSize(window.innerWidth, window.innerHeight);
        this.renderer.setPixelRatio(window.devicePixelRatio);
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    }

    initLighting() {
        const ambientLight = new THREE.AmbientLight(0xffffff, 0.4);
        this.scene.add(ambientLight);
        
        const directionalLight = new THREE.DirectionalLight(0xffffff, 0.8);
        directionalLight.position.set(50, 100, 50);
        directionalLight.castShadow = true;
        directionalLight.shadow.mapSize.width = 2048;
        directionalLight.shadow.mapSize.height = 2048;
        directionalLight.shadow.camera.near = 0.5;
        directionalLight.shadow.camera.far = 500;
        directionalLight.shadow.camera.left = -200;
        directionalLight.shadow.camera.right = 200;
        directionalLight.shadow.camera.top = 200;
        directionalLight.shadow.camera.bottom = -200;
        this.scene.add(directionalLight);
        
        const hemisphereLight = new THREE.HemisphereLight(0x87ceeb, 0x1a472a, 0.3);
        this.scene.add(hemisphereLight);
    }

    initTrack() {
        this.track = new TrackGenerator(this.scene);
        this.track.generateRandomTrack(Date.now());
    }

    initVehicle() {
        this.vehicle = new VehiclePhysics(this.scene, this.track);
        
        const startPosition = this.track.getStartPosition();
        this.vehicle.setPosition(startPosition.position, startPosition.direction);
        this.lastVehiclePosition.copy(startPosition.position);
    }

    initCollisionDetector() {
        this.collisionDetector = new CollisionDetector(
            this.scene,
            this.vehicle,
            this.track
        );
    }

    initScoreManager() {
        this.scoreManager = new ScoreManager();
    }

    initCameraController() {
        this.cameraController = new CameraController(this.camera, this.vehicle);
    }

    initReplayManager() {
        this.replayManager = new ReplayManager();
    }

    initMinimap() {
        this.minimapCanvas = document.getElementById('minimap');
        this.minimapCanvas.width = 200;
        this.minimapCanvas.height = 200;
        this.minimapCtx = this.minimapCanvas.getContext('2d');
    }

    initEventListeners() {
        document.addEventListener('keydown', (e) => {
            this.keys[e.code] = true;
            this.handleKeyDown(e);
        });
        
        document.addEventListener('keyup', (e) => {
            this.keys[e.code] = false;
            this.handleKeyUp(e);
        });
        
        window.addEventListener('resize', () => this.onWindowResize());
        
        document.getElementById('start-btn').addEventListener('click', () => this.startGame());
        document.getElementById('restart-btn').addEventListener('click', () => this.restartGame());
        document.getElementById('resume-btn').addEventListener('click', () => this.resumeGame());
        document.getElementById('quit-btn').addEventListener('click', () => this.quitToMenu());
    }

    initUI() {
        const style = document.createElement('style');
        style.textContent = `
            @keyframes floatUp {
                0% {
                    opacity: 1;
                    transform: translate(-50%, -50%) translateY(0);
                }
                100% {
                    opacity: 0;
                    transform: translate(-50%, -50%) translateY(-100px);
                }
            }
            
            .nitro-bar {
                position: absolute;
                bottom: 20px;
                left: 50%;
                transform: translateX(-50%);
                width: 200px;
                height: 20px;
                background: rgba(0, 0, 0, 0.6);
                border-radius: 10px;
                border: 2px solid rgba(0, 255, 255, 0.3);
                overflow: hidden;
            }
            
            .nitro-bar-fill {
                height: 100%;
                background: linear-gradient(90deg, #0ff, #08f);
                transition: width 0.1s;
                box-shadow: 0 0 10px #0ff;
            }
            
            .nitro-bar-label {
                position: absolute;
                width: 100%;
                text-align: center;
                color: #fff;
                font-size: 12px;
                line-height: 20px;
                text-shadow: 1px 1px 2px rgba(0, 0, 0, 0.8);
            }
            
            .recording-indicator {
                position: absolute;
                top: 80px;
                left: 20px;
                background: rgba(255, 0, 0, 0.8);
                padding: 8px 15px;
                border-radius: 5px;
                color: #fff;
                font-size: 14px;
                display: none;
                animation: pulse 1s infinite;
            }
            
            .replay-controls {
                position: absolute;
                bottom: 100px;
                left: 50%;
                transform: translateX(-50%);
                display: none;
                gap: 10px;
            }
            
            .replay-btn {
                background: rgba(0, 0, 0, 0.7);
                color: #fff;
                border: 1px solid rgba(255, 255, 255, 0.3);
                padding: 8px 15px;
                border-radius: 5px;
                cursor: pointer;
            }
            
            .replay-btn:hover {
                background: rgba(0, 255, 255, 0.3);
            }
            
            .replay-list {
                max-height: 200px;
                overflow-y: auto;
                margin: 10px 0;
            }
            
            .replay-item {
                padding: 10px;
                background: rgba(0, 0, 0, 0.3);
                margin: 5px 0;
                border-radius: 5px;
                cursor: pointer;
                display: flex;
                justify-content: space-between;
                align-items: center;
            }
            
            .replay-item:hover {
                background: rgba(0, 255, 255, 0.2);
            }
            
            .replay-item-info {
                font-size: 12px;
                color: #aaa;
            }
        `;
        document.head.appendChild(style);
        
        this.createNitroBar();
        this.createRecordingIndicator();
    }

    createNitroBar() {
        const nitroBar = document.createElement('div');
        nitroBar.id = 'nitro-bar';
        nitroBar.className = 'nitro-bar';
        nitroBar.innerHTML = `
            <div class="nitro-bar-fill" id="nitro-bar-fill" style="width: 100%"></div>
            <div class="nitro-bar-label">氮气 100%</div>
        `;
        document.getElementById('ui-overlay').appendChild(nitroBar);
    }

    createRecordingIndicator() {
        const indicator = document.createElement('div');
        indicator.id = 'recording-indicator';
        indicator.className = 'recording-indicator';
        indicator.innerHTML = '● 录像中';
        document.getElementById('ui-overlay').appendChild(indicator);
    }

    handleKeyDown(e) {
        if (e.code === 'KeyC' && this.isRunning && !this.isPaused && !this.isReplayMode) {
            this.cameraController.switchCameraMode();
        }
        
        if (e.code === 'KeyR' && this.isRunning && !this.isPaused && !this.isReplayMode) {
            this.resetVehiclePosition();
        }
        
        if (e.code === 'Space' && this.isRunning && !this.isPaused && !this.isReplayMode) {
            this.keys['Space'] = true;
        }
        
        if (e.code === 'KeyF') {
            if (this.isRunning && !this.isPaused && !this.isReplayMode) {
                this.toggleRecording();
            }
        }
        
        if (e.code === 'KeyG' && !this.isReplayMode) {
            this.showReplayMenu();
        }
        
        if (e.code === 'Escape') {
            if (this.isReplayMode) {
                this.stopReplay();
            } else if (this.isRunning && !this.isPaused) {
                this.pauseGame();
            } else if (this.isPaused) {
                this.resumeGame();
            }
        }
    }

    handleKeyUp(e) {
        if (e.code === 'Space') {
            this.keys['Space'] = false;
        }
    }

    toggleRecording() {
        if (this.replayManager.isRecording) {
            this.replayManager.stopRecording();
            document.getElementById('recording-indicator').style.display = 'none';
            
            const name = prompt('输入录像名称：', `竞速录像 ${new Date().toLocaleString('zh-CN')}`);
            if (name) {
                this.replayManager.saveCurrentReplay(name);
                this.scoreManager.showFloatingScore('录像已保存！');
            }
        } else {
            this.replayManager.startRecording();
            document.getElementById('recording-indicator').style.display = 'block';
            this.scoreManager.showFloatingScore('开始录像');
        }
    }

    showReplayMenu() {
        const replays = this.replayManager.getSavedReplays();
        
        if (replays.length === 0) {
            this.scoreManager.showFloatingScore('暂无录像');
            return;
        }
        
        let replayHTML = '<div class="replay-list">';
        replays.forEach(replay => {
            replayHTML += `
                <div class="replay-item" onclick="game.loadAndPlayReplay(${replay.index})">
                    <span>${replay.name}</span>
                    <span class="replay-item-info">${replay.duration}秒 | ${replay.frameCount}帧</span>
                </div>
            `;
        });
        replayHTML += '</div>';
        
        this.scoreManager.showFloatingScore('按 G 选择录像回放');
    }

    loadAndPlayReplay(index) {
        if (this.replayManager.loadReplay(index)) {
            this.startReplay();
        }
    }

    startReplay() {
        if (this.replayManager.startPlayback()) {
            this.isReplayMode = true;
            this.isRunning = false;
            this.isPaused = false;
            this.scoreManager.showFloatingScore('回放开始');
        }
    }

    stopReplay() {
        this.replayManager.stopPlayback();
        this.isReplayMode = false;
        this.scoreManager.showFloatingScore('回放结束');
        
        const startPosition = this.track.getStartPosition();
        this.vehicle.setPosition(startPosition.position, startPosition.direction);
    }

    updateInput() {
        const input = {
            accelerate: this.keys['KeyW'] || this.keys['ArrowUp'],
            brake: this.keys['KeyS'] || this.keys['ArrowDown'],
            left: this.keys['KeyA'] || this.keys['ArrowLeft'],
            right: this.keys['KeyD'] || this.keys['ArrowRight'],
            nitro: this.keys['Space']
        };
        
        this.vehicle.setInput(input);
    }

    startGame() {
        document.getElementById('start-screen').classList.add('hidden');
        document.getElementById('finish-screen').classList.add('hidden');
        
        this.scoreManager.startGame();
        this.collisionDetector.playStartSound();
        this.isRunning = true;
        this.isPaused = false;
        this.isReplayMode = false;
        this.lastTime = performance.now();
        this.lastVehiclePosition.copy(this.vehicle.getPosition());
        
        if (this.replayManager.isRecording) {
            this.replayManager.stopRecording();
            document.getElementById('recording-indicator').style.display = 'none';
        }
    }

    pauseGame() {
        this.isPaused = true;
        document.getElementById('pause-screen').classList.remove('hidden');
    }

    resumeGame() {
        this.isPaused = false;
        document.getElementById('pause-screen').classList.add('hidden');
        this.lastTime = performance.now();
    }

    quitToMenu() {
        this.isRunning = false;
        this.isPaused = false;
        this.isReplayMode = false;
        document.getElementById('pause-screen').classList.add('hidden');
        document.getElementById('start-screen').classList.remove('hidden');
        this.resetGame();
    }

    restartGame() {
        document.getElementById('finish-screen').classList.add('hidden');
        this.resetGame();
        this.startGame();
    }

    resetGame() {
        this.track.clearTrack();
        this.track.generateRandomTrack(Date.now());
        
        const startPosition = this.track.getStartPosition();
        this.vehicle.setPosition(startPosition.position, startPosition.direction);
        this.lastVehiclePosition.copy(startPosition.position);
        this.vehicle.reset();
        
        this.collisionDetector.reset();
        this.scoreManager.reset();
        this.cameraController.reset();
        
        this.lastMinimapUpdate = 0;
        
        if (this.replayManager.isRecording) {
            this.replayManager.stopRecording();
            document.getElementById('recording-indicator').style.display = 'none';
        }
    }

    resetVehiclePosition() {
        const nearestPoint = this.track.getClosestTrackPoint(this.vehicle.getPosition());
        const trackPosition = this.track.getTrackPointAt(nearestPoint.t);
        const trackDirection = this.track.getTrackTangentAt(nearestPoint.t);
        
        this.vehicle.setPosition(trackPosition, trackDirection);
        this.vehicle.reset();
        this.lastVehiclePosition.copy(trackPosition);
    }

    update(deltaTime) {
        if (this.isReplayMode) {
            this.updateReplay(deltaTime);
            return;
        }
        
        if (!this.isRunning || this.isPaused) return;
        
        this.currentTime = Date.now();
        const currentTimeSeconds = this.currentTime / 1000;
        
        this.updateInput();
        this.vehicle.update(deltaTime, currentTimeSeconds);
        
        this.track.refreshObstacles(currentTimeSeconds);
        this.track.updateNitroPickups(this.currentTime);
        
        const vehiclePosition = this.vehicle.getPosition();
        
        if (this.track.checkNitroPickup(vehiclePosition)) {
            this.vehicle.addNitro(30);
            this.scoreManager.showFloatingScore('+30 氮气!');
        }
        
        const collisions = this.collisionDetector.checkCollisions(currentTimeSeconds, deltaTime);
        
        if (collisions.length > 0) {
            this.cameraController.triggerShake(0.3);
        }
        
        const passedCheckpoints = this.collisionDetector.checkCheckpoints(vehiclePosition);
        
        if (passedCheckpoints.length > 0) {
            passedCheckpoints.forEach(() => {
                this.scoreManager.addCheckpointScore();
            });
        }
        
        if (this.collisionDetector.checkStartLine(vehiclePosition, this.lastVehiclePosition)) {
            if (this.scoreManager.getLap() > 0 || this.currentTime - this.scoreManager.startTime > 5000) {
                this.scoreManager.addLapScore();
                
                if (this.scoreManager.isFinished()) {
                    this.finishGame();
                }
            }
        }
        
        this.lastVehiclePosition.copy(vehiclePosition);
        this.collisionDetector.updateCollisionEffects(deltaTime);
        this.scoreManager.update(this.vehicle.getSpeed(), this.collisionDetector.getCollisionCount(), this.currentTime);
        
        if (this.replayManager.isRecording) {
            this.replayManager.recordFrame({
                position: this.vehicle.position,
                heading: this.vehicle.heading,
                speed: this.vehicle.speed,
                nitroAmount: this.vehicle.nitroAmount,
                nitroActive: this.vehicle.nitroActive
            });
        }
        
        this.updateNitroUI();
        
        if (currentTimeSeconds - this.lastMinimapUpdate > this.minimapUpdateInterval) {
            this.track.renderMinimap(
                this.minimapCtx,
                this.minimapCanvas.width,
                this.minimapCanvas.height,
                vehiclePosition
            );
            this.lastMinimapUpdate = currentTimeSeconds;
        }
    }

    updateReplay(deltaTime) {
        const frame = this.replayManager.getPlaybackFrame();
        
        if (!frame) {
            this.stopReplay();
            return;
        }
        
        this.vehicle.position.set(frame.position.x, frame.position.y, frame.position.z);
        this.vehicle.heading = frame.heading;
        this.vehicle.speed = frame.speed;
        this.vehicle.nitroAmount = frame.nitroAmount;
        
        if (frame.nitroActive && !this.vehicle.nitroActive) {
            this.vehicle.activateNitro();
        } else if (!frame.nitroActive && this.vehicle.nitroActive) {
            this.vehicle.deactivateNitro();
        }
        
        this.vehicle.updateVehicleTransform();
        this.vehicle.updateNitroEffect(deltaTime);
        
        this.updateNitroUI();
        
        const currentTimeSeconds = Date.now() / 1000;
        if (currentTimeSeconds - this.lastMinimapUpdate > this.minimapUpdateInterval) {
            this.track.renderMinimap(
                this.minimapCtx,
                this.minimapCanvas.width,
                this.minimapCanvas.height,
                this.vehicle.position
            );
            this.lastMinimapUpdate = currentTimeSeconds;
        }
    }

    updateNitroUI() {
        const nitroFill = document.getElementById('nitro-bar-fill');
        const nitroLabel = document.querySelector('#nitro-bar .nitro-bar-label');
        
        if (nitroFill && nitroLabel) {
            const percentage = this.vehicle.getNitroPercentage();
            nitroFill.style.width = percentage + '%';
            nitroLabel.textContent = `氮气 ${Math.floor(percentage)}%`;
            
            if (this.vehicle.nitroActive) {
                nitroFill.style.background = 'linear-gradient(90deg, #ff0, #f80)';
                nitroFill.style.boxShadow = '0 0 20px #ff0';
            } else {
                nitroFill.style.background = 'linear-gradient(90deg, #0ff, #08f)';
                nitroFill.style.boxShadow = '0 0 10px #0ff';
            }
        }
    }

    finishGame() {
        this.isRunning = false;
        this.collisionDetector.playFinishSound();
        
        if (this.replayManager.isRecording) {
            this.replayManager.stopRecording();
            document.getElementById('recording-indicator').style.display = 'none';
            
            if (confirm('是否保存本次录像？')) {
                const name = prompt('输入录像名称：', `竞速录像 ${new Date().toLocaleString('zh-CN')}`);
                if (name) {
                    this.replayManager.saveCurrentReplay(name);
                }
            }
        }
        
        document.getElementById('finish-screen').classList.remove('hidden');
    }

    animate() {
        requestAnimationFrame(() => this.animate());
        
        const currentTime = performance.now();
        const deltaTime = Math.min((currentTime - this.lastTime) / 1000, 0.1);
        this.lastTime = currentTime;
        
        if (this.isReplayMode) {
            this.update(deltaTime);
            this.cameraController.update(deltaTime);
        } else if (this.isRunning && !this.isPaused) {
            this.update(deltaTime);
            this.cameraController.update(deltaTime);
        }
        
        this.renderer.render(this.scene, this.camera);
    }

    onWindowResize() {
        this.camera.aspect = window.innerWidth / window.innerHeight;
        this.camera.updateProjectionMatrix();
        this.renderer.setSize(window.innerWidth, window.innerHeight);
    }
}

let game;
window.addEventListener('DOMContentLoaded', () => {
    game = new Game();
});
