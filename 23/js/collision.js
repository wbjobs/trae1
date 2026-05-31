class CollisionDetector {
    constructor(scene, vehicle, track) {
        this.scene = scene;
        this.vehicle = vehicle;
        this.track = track;
        
        this.collisionCount = 0;
        this.lastCollisionTime = 0;
        this.collisionCooldown = 0.3;
        
        this.collisionEffects = [];
        this.collisionParticles = [];
        
        this.lastVehiclePosition = new THREE.Vector3();
        this.vehicleRadius = 2;
        
        this.audioContext = null;
        this.initAudio();
    }

    initAudio() {
        try {
            this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
        } catch (e) {
            console.log('Audio not supported');
        }
    }

    playCollisionSound() {
        if (!this.audioContext) return;
        
        const oscillator = this.audioContext.createOscillator();
        const gainNode = this.audioContext.createGain();
        
        oscillator.connect(gainNode);
        gainNode.connect(this.audioContext.destination);
        
        oscillator.frequency.setValueAtTime(200, this.audioContext.currentTime);
        oscillator.frequency.exponentialRampToValueAtTime(50, this.audioContext.currentTime + 0.2);
        
        gainNode.gain.setValueAtTime(0.3, this.audioContext.currentTime);
        gainNode.gain.exponentialRampToValueAtTime(0.01, this.audioContext.currentTime + 0.3);
        
        oscillator.start(this.audioContext.currentTime);
        oscillator.stop(this.audioContext.currentTime + 0.3);
    }

    playCheckpointSound() {
        if (!this.audioContext) return;
        
        const oscillator = this.audioContext.createOscillator();
        const gainNode = this.audioContext.createGain();
        
        oscillator.connect(gainNode);
        gainNode.connect(this.audioContext.destination);
        
        oscillator.frequency.setValueAtTime(440, this.audioContext.currentTime);
        oscillator.frequency.setValueAtTime(554, this.audioContext.currentTime + 0.1);
        oscillator.frequency.setValueAtTime(659, this.audioContext.currentTime + 0.2);
        
        gainNode.gain.setValueAtTime(0.2, this.audioContext.currentTime);
        gainNode.gain.exponentialRampToValueAtTime(0.01, this.audioContext.currentTime + 0.4);
        
        oscillator.start(this.audioContext.currentTime);
        oscillator.stop(this.audioContext.currentTime + 0.4);
    }

    playStartSound() {
        if (!this.audioContext) return;
        
        const oscillator = this.audioContext.createOscillator();
        const gainNode = this.audioContext.createGain();
        
        oscillator.connect(gainNode);
        gainNode.connect(this.audioContext.destination);
        
        oscillator.frequency.setValueAtTime(523, this.audioContext.currentTime);
        oscillator.frequency.setValueAtTime(659, this.audioContext.currentTime + 0.15);
        oscillator.frequency.setValueAtTime(784, this.audioContext.currentTime + 0.3);
        
        gainNode.gain.setValueAtTime(0.3, this.audioContext.currentTime);
        gainNode.gain.exponentialRampToValueAtTime(0.01, this.audioContext.currentTime + 0.5);
        
        oscillator.start(this.audioContext.currentTime);
        oscillator.stop(this.audioContext.currentTime + 0.5);
    }

    playFinishSound() {
        if (!this.audioContext) return;
        
        const notes = [523, 587, 659, 698, 784, 880, 988, 1047];
        
        notes.forEach((freq, i) => {
            const oscillator = this.audioContext.createOscillator();
            const gainNode = this.audioContext.createGain();
            
            oscillator.connect(gainNode);
            gainNode.connect(this.audioContext.destination);
            
            oscillator.frequency.setValueAtTime(freq, this.audioContext.currentTime + i * 0.1);
            
            gainNode.gain.setValueAtTime(0.2, this.audioContext.currentTime + i * 0.1);
            gainNode.gain.exponentialRampToValueAtTime(0.01, this.audioContext.currentTime + i * 0.1 + 0.2);
            
            oscillator.start(this.audioContext.currentTime + i * 0.1);
            oscillator.stop(this.audioContext.currentTime + i * 0.1 + 0.2);
        });
    }

    checkCollisions(currentTime, deltaTime) {
        const collisions = [];
        
        const vehiclePosition = this.vehicle.getPosition();
        const vehicleDirection = this.vehicle.getDirection();
        
        const raycaster = new THREE.Raycaster(
            this.lastVehiclePosition.clone(),
            vehiclePosition.clone().sub(this.lastVehiclePosition).normalize(),
            0,
            this.lastVehiclePosition.distanceTo(vehiclePosition) + this.vehicleRadius
        );
        
        this.track.obstacles.forEach((obstacle, index) => {
            if (!obstacle.userData.active) return;
            
            const distance = vehiclePosition.distanceTo(obstacle.position);
            
            if (distance < this.vehicleRadius + 1.5) {
                const collisionNormal = this.calculateCollisionNormal(
                    vehiclePosition,
                    obstacle.position
                );
                
                collisions.push({
                    type: 'obstacle',
                    object: obstacle,
                    normal: collisionNormal,
                    force: this.vehicle.getSpeed() * 0.5
                });
                
                obstacle.userData.active = false;
                obstacle.visible = false;
                
                this.createCollisionEffect(obstacle.position.clone());
                
                this.pushVehicleBack(obstacle.position, collisionNormal);
            }
        });
        
        this.track.barriers.forEach((barrier) => {
            const distance = vehiclePosition.distanceTo(barrier.position);
            
            if (distance < this.vehicleRadius + 1) {
                const collisionNormal = this.calculateCollisionNormal(
                    vehiclePosition,
                    barrier.position
                );
                
                collisions.push({
                    type: 'barrier',
                    object: barrier,
                    normal: collisionNormal,
                    force: this.vehicle.getSpeed() * 0.3
                });
                
                this.pushVehicleBack(barrier.position, collisionNormal);
            }
        });
        
        this.checkTrackBounds(vehiclePosition, collisions);
        
        if (collisions.length > 0) {
            const now = currentTime || Date.now() / 1000;
            if (now - this.lastCollisionTime > this.collisionCooldown) {
                this.collisionCount++;
                this.lastCollisionTime = now;
                this.playCollisionSound();
                
                this.flashScreen();
            }
            
            const mainCollision = collisions[0];
            this.vehicle.applyCollision(mainCollision.normal, mainCollision.force);
        }
        
        this.lastVehiclePosition.copy(vehiclePosition);
        
        return collisions;
    }

    pushVehicleBack(obstaclePos, collisionNormal) {
        const vehiclePos = this.vehicle.getPosition();
        const pushDistance = 2;
        
        const newPos = vehiclePos.clone().add(
            collisionNormal.clone().multiplyScalar(pushDistance)
        );
        
        this.vehicle.position.copy(newPos);
        this.vehicle.velocity.multiplyScalar(0.5);
    }

    checkTrackBounds(vehiclePosition, collisions) {
        if (!this.track.curve) return;
        
        const trackInfo = this.track.getClosestTrackPoint(vehiclePosition);
        
        if (trackInfo.distance > this.track.trackWidth / 2 + 3) {
            const trackPoint = trackInfo.point;
            const directionToTrack = trackPoint.clone().sub(vehiclePosition).normalize();
            
            collisions.push({
                type: 'offtrack',
                object: null,
                normal: directionToTrack,
                force: 5
            });
            
            const pullBack = vehiclePosition.clone().lerp(trackPoint, 0.1);
            pullBack.y = vehiclePosition.y;
            this.vehicle.position.copy(pullBack);
        }
    }

    calculateCollisionNormal(vehiclePos, obstaclePos) {
        const normal = vehiclePos.clone().sub(obstaclePos);
        normal.y = 0;
        if (normal.length() < 0.01) {
            normal.set(Math.random() - 0.5, 0, Math.random() - 0.5);
        }
        normal.normalize();
        return normal;
    }

    checkCheckpoints(vehiclePosition) {
        const passedCheckpoints = [];
        
        this.track.checkpoints.forEach((checkpoint) => {
            if (checkpoint.userData.passed) return;
            
            const distance = vehiclePosition.distanceTo(checkpoint.position);
            
            if (distance < 6) {
                checkpoint.userData.passed = true;
                checkpoint.material.color.setHex(0x00ff00);
                checkpoint.material.emissive.setHex(0x00ff00);
                checkpoint.material.opacity = 0.3;
                
                this.playCheckpointSound();
                passedCheckpoints.push(checkpoint);
            }
        });
        
        return passedCheckpoints;
    }

    checkStartLine(vehiclePosition, lastPosition) {
        const startLinePosition = this.track.startLine.position;
        const distance = vehiclePosition.distanceTo(startLinePosition);
        
        if (distance < 5) {
            const lastDistance = lastPosition.distanceTo(startLinePosition);
            
            if (lastDistance > distance && lastDistance > 10) {
                return true;
            }
        }
        
        return false;
    }

    createCollisionEffect(position) {
        const particleCount = 20;
        const geometry = new THREE.BufferGeometry();
        const positions = new Float32Array(particleCount * 3);
        const velocities = [];
        
        for (let i = 0; i < particleCount; i++) {
            positions[i * 3] = position.x;
            positions[i * 3 + 1] = position.y + 0.5;
            positions[i * 3 + 2] = position.z;
            
            velocities.push(new THREE.Vector3(
                (Math.random() - 0.5) * 10,
                Math.random() * 5 + 2,
                (Math.random() - 0.5) * 10
            ));
        }
        
        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        
        const material = new THREE.PointsMaterial({
            color: 0xff6600,
            size: 0.3,
            transparent: true,
            opacity: 1
        });
        
        const particles = new THREE.Points(geometry, material);
        this.scene.add(particles);
        
        this.collisionParticles.push({
            particles,
            velocities,
            life: 1.0,
            startTime: Date.now()
        });
    }

    updateCollisionEffects(deltaTime) {
        for (let i = this.collisionParticles.length - 1; i >= 0; i--) {
            const effect = this.collisionParticles[i];
            const elapsed = (Date.now() - effect.startTime) / 1000;
            
            if (elapsed > 1.0) {
                this.scene.remove(effect.particles);
                this.collisionParticles.splice(i, 1);
                continue;
            }
            
            const positions = effect.particles.geometry.attributes.position.array;
            
            for (let j = 0; j < effect.velocities.length; j++) {
                positions[j * 3] += effect.velocities[j].x * deltaTime;
                positions[j * 3 + 1] += effect.velocities[j].y * deltaTime;
                positions[j * 3 + 2] += effect.velocities[j].z * deltaTime;
                
                effect.velocities[j].y -= 10 * deltaTime;
            }
            
            effect.particles.geometry.attributes.position.needsUpdate = true;
            effect.particles.material.opacity = 1.0 - elapsed;
        }
    }

    flashScreen() {
        const container = document.getElementById('game-container');
        if (container) {
            container.classList.add('collision-flash');
            setTimeout(() => {
                container.classList.remove('collision-flash');
            }, 300);
        }
    }

    getCollisionCount() {
        return this.collisionCount;
    }

    reset() {
        this.collisionCount = 0;
        this.lastCollisionTime = 0;
        
        this.track.obstacles.forEach(obstacle => {
            obstacle.userData.active = true;
            obstacle.visible = true;
        });
        
        this.track.checkpoints.forEach(checkpoint => {
            checkpoint.userData.passed = false;
            checkpoint.material.color.setHex(0x00ff00);
            checkpoint.material.emissive.setHex(0x00ff00);
            checkpoint.material.opacity = 0.6;
        });
        
        for (let i = this.collisionParticles.length - 1; i >= 0; i--) {
            this.scene.remove(this.collisionParticles[i].particles);
        }
        this.collisionParticles = [];
        
        this.lastVehiclePosition.copy(this.vehicle.getPosition());
    }
}
