class VehiclePhysics {
    constructor(scene, track) {
        this.scene = scene;
        this.track = track;
        
        this.vehicle = null;
        this.wheels = [];
        this.exhaustParticles = null;
        this.nitroFlame = null;
        
        this.position = new THREE.Vector3();
        this.velocity = new THREE.Vector3();
        this.acceleration = new THREE.Vector3();
        this.direction = new THREE.Vector3(0, 0, 1);
        
        this.speed = 0;
        this.maxSpeed = 60;
        this.accelerationForce = 35;
        this.brakeForce = 45;
        this.friction = 0.985;
        this.turnSpeed = 3;
        this.mass = 1000;
        this.inertia = 0.93;
        this.angularVelocity = 0;
        this.heading = 0;
        
        this.gravity = -9.8;
        this.isGrounded = true;
        this.verticalVelocity = 0;
        
        this.driftFactor = 0.85;
        this.lateralGrip = 0.85;
        
        this.slideAngle = 0;
        this.isSliding = false;
        
        this.nitroActive = false;
        this.nitroAmount = 100;
        this.maxNitro = 100;
        this.nitroRegenRate = 5;
        this.nitroConsumeRate = 30;
        this.nitroBoostMultiplier = 1.8;
        this.nitroMaxSpeed = 100;
        
        this.cachedTrackInfo = null;
        this.cacheUpdateInterval = 0.05;
        this.lastCacheUpdate = 0;
        
        this.input = {
            accelerate: false,
            brake: false,
            left: false,
            right: false,
            nitro: false
        };
        
        this.createVehicle();
        this.createNitroEffect();
    }

    createVehicle() {
        this.vehicle = new THREE.Group();
        
        const bodyGeometry = new THREE.BoxGeometry(2, 0.6, 4);
        const bodyMaterial = new THREE.MeshStandardMaterial({
            color: 0xff3333,
            roughness: 0.3,
            metalness: 0.7
        });
        const body = new THREE.Mesh(bodyGeometry, bodyMaterial);
        body.position.y = 0.5;
        body.castShadow = true;
        this.vehicle.add(body);
        
        const cabinGeometry = new THREE.BoxGeometry(1.5, 0.5, 2);
        const cabinMaterial = new THREE.MeshStandardMaterial({
            color: 0x333333,
            roughness: 0.1,
            metalness: 0.9,
            transparent: true,
            opacity: 0.8
        });
        const cabin = new THREE.Mesh(cabinGeometry, cabinMaterial);
        cabin.position.set(0, 0.9, -0.3);
        cabin.castShadow = true;
        this.vehicle.add(cabin);
        
        const wheelGeometry = new THREE.CylinderGeometry(0.3, 0.3, 0.3, 16);
        const wheelMaterial = new THREE.MeshStandardMaterial({
            color: 0x222222,
            roughness: 0.9
        });
        
        const wheelPositions = [
            new THREE.Vector3(-0.8, 0.3, 1.3),
            new THREE.Vector3(0.8, 0.3, 1.3),
            new THREE.Vector3(-0.8, 0.3, -1.3),
            new THREE.Vector3(0.8, 0.3, -1.3)
        ];
        
        wheelPositions.forEach(pos => {
            const wheel = new THREE.Mesh(wheelGeometry, wheelMaterial);
            wheel.rotation.z = Math.PI / 2;
            wheel.position.copy(pos);
            wheel.castShadow = true;
            this.vehicle.add(wheel);
            this.wheels.push(wheel);
        });
        
        const headlightGeometry = new THREE.SphereGeometry(0.15, 8, 8);
        const headlightMaterial = new THREE.MeshStandardMaterial({
            color: 0xffffaa,
            emissive: 0xffffaa,
            emissiveIntensity: 1
        });
        
        const leftHeadlight = new THREE.Mesh(headlightGeometry, headlightMaterial);
        leftHeadlight.position.set(-0.6, 0.5, 2);
        this.vehicle.add(leftHeadlight);
        
        const rightHeadlight = new THREE.Mesh(headlightGeometry, headlightMaterial);
        rightHeadlight.position.set(0.6, 0.5, 2);
        this.vehicle.add(rightHeadlight);
        
        const taillightMaterial = new THREE.MeshStandardMaterial({
            color: 0xff0000,
            emissive: 0xff0000,
            emissiveIntensity: 0.5
        });
        
        const leftTaillight = new THREE.Mesh(headlightGeometry, taillightMaterial);
        leftTaillight.position.set(-0.6, 0.5, -2);
        this.vehicle.add(leftTaillight);
        
        const rightTaillight = new THREE.Mesh(headlightGeometry, taillightMaterial);
        rightTaillight.position.set(0.6, 0.5, -2);
        this.vehicle.add(rightTaillight);
        
        const exhaustGeometry = new THREE.CylinderGeometry(0.1, 0.15, 0.3, 8);
        const exhaustMaterial = new THREE.MeshStandardMaterial({
            color: 0x444444,
            metalness: 0.8
        });
        const exhaust = new THREE.Mesh(exhaustGeometry, exhaustMaterial);
        exhaust.rotation.x = Math.PI / 2;
        exhaust.position.set(0, 0.3, -2.2);
        this.vehicle.add(exhaust);
        
        this.vehicle.traverse((child) => {
            if (child.isMesh) {
                child.castShadow = true;
            }
        });
        
        this.scene.add(this.vehicle);
    }

    createNitroEffect() {
        const flameGeometry = new THREE.ConeGeometry(0.15, 0.8, 8);
        const flameMaterial = new THREE.MeshBasicMaterial({
            color: 0x00ffff,
            transparent: true,
            opacity: 0.8,
            side: THREE.DoubleSide
        });
        
        this.nitroFlame = new THREE.Mesh(flameGeometry, flameMaterial);
        this.nitroFlame.rotation.x = Math.PI;
        this.nitroFlame.position.set(0, 0.3, -2.5);
        this.nitroFlame.visible = false;
        this.vehicle.add(this.nitroFlame);
        
        const glowGeometry = new THREE.SphereGeometry(0.3, 8, 8);
        const glowMaterial = new THREE.MeshBasicMaterial({
            color: 0x00ffff,
            transparent: true,
            opacity: 0.5
        });
        
        const glow = new THREE.Mesh(glowGeometry, glowMaterial);
        glow.position.set(0, 0.3, -2.2);
        this.nitroFlame.add(glow);
        
        const particleCount = 50;
        const positions = new Float32Array(particleCount * 3);
        const colors = new Float32Array(particleCount * 3);
        
        for (let i = 0; i < particleCount; i++) {
            positions[i * 3] = 0;
            positions[i * 3 + 1] = 0.3;
            positions[i * 3 + 2] = -2.5;
            
            colors[i * 3] = 0;
            colors[i * 3 + 1] = 1;
            colors[i * 3 + 2] = 1;
        }
        
        const particleGeometry = new THREE.BufferGeometry();
        particleGeometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        particleGeometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        
        const particleMaterial = new THREE.PointsMaterial({
            size: 0.1,
            vertexColors: true,
            transparent: true,
            opacity: 0.8,
            blending: THREE.AdditiveBlending
        });
        
        this.exhaustParticles = new THREE.Points(particleGeometry, particleMaterial);
        this.exhaustParticles.visible = false;
        this.vehicle.add(this.exhaustParticles);
    }

    updateNitroEffect(deltaTime) {
        if (this.nitroActive && this.nitroFlame.visible) {
            this.nitroFlame.scale.y = 1 + Math.sin(Date.now() * 0.02) * 0.3;
            this.nitroFlame.material.opacity = 0.6 + Math.random() * 0.4;
            
            if (this.exhaustParticles) {
                const positions = this.exhaustParticles.geometry.attributes.position.array;
                for (let i = 0; i < positions.length / 3; i++) {
                    positions[i * 3] += (Math.random() - 0.5) * 0.1;
                    positions[i * 3 + 1] += (Math.random() - 0.5) * 0.1;
                    positions[i * 3 + 2] -= Math.random() * 0.2;
                    
                    if (positions[i * 3 + 2] < -10) {
                        positions[i * 3] = 0;
                        positions[i * 3 + 1] = 0.3;
                        positions[i * 3 + 2] = -2.5;
                    }
                }
                this.exhaustParticles.geometry.attributes.position.needsUpdate = true;
            }
        }
    }

    activateNitro() {
        if (this.nitroAmount > 0 && !this.nitroActive) {
            this.nitroActive = true;
            this.nitroFlame.visible = true;
            this.exhaustParticles.visible = true;
        }
    }

    deactivateNitro() {
        this.nitroActive = false;
        if (this.nitroFlame) this.nitroFlame.visible = false;
        if (this.exhaustParticles) this.exhaustParticles.visible = false;
    }

    addNitro(amount = 30) {
        this.nitroAmount = Math.min(this.maxNitro, this.nitroAmount + amount);
    }

    getNitroPercentage() {
        return (this.nitroAmount / this.maxNitro) * 100;
    }

    setPosition(position, direction) {
        this.position.copy(position);
        this.direction.copy(direction).normalize();
        this.heading = Math.atan2(this.direction.x, this.direction.z);
        this.velocity.set(0, 0, 0);
        this.speed = 0;
        this.angularVelocity = 0;
        this.cachedTrackInfo = null;
        this.updateVehicleTransform();
    }

    getCachedTrackInfo(currentTime) {
        if (!this.track || !this.track.curve) return null;
        
        if (this.cachedTrackInfo && (currentTime - this.lastCacheUpdate) < this.cacheUpdateInterval) {
            return this.cachedTrackInfo;
        }
        
        this.cachedTrackInfo = this.track.getClosestTrackPoint(this.position);
        this.lastCacheUpdate = currentTime;
        
        return this.cachedTrackInfo;
    }

    update(deltaTime, currentTime) {
        currentTime = currentTime || Date.now() / 1000;
        
        this.updateNitro(deltaTime);
        this.updateAcceleration(deltaTime);
        this.updateRotation(deltaTime);
        this.updateVelocity(deltaTime, currentTime);
        this.updateVerticalPosition(deltaTime, currentTime);
        this.updateVehicleTransform();
        this.updateWheels(deltaTime);
        this.updateNitroEffect(deltaTime);
    }

    updateNitro(deltaTime) {
        if (this.input.nitro && this.nitroAmount > 0 && this.input.accelerate) {
            this.activateNitro();
        } else {
            this.deactivateNitro();
        }
        
        if (this.nitroActive) {
            this.nitroAmount -= this.nitroConsumeRate * deltaTime;
            if (this.nitroAmount <= 0) {
                this.nitroAmount = 0;
                this.deactivateNitro();
            }
        } else {
            this.nitroAmount += this.nitroRegenRate * deltaTime;
            this.nitroAmount = Math.min(this.maxNitro, this.nitroAmount);
        }
    }

    updateAcceleration(deltaTime) {
        const currentAcceleration = this.nitroActive 
            ? this.accelerationForce * this.nitroBoostMultiplier 
            : this.accelerationForce;
        const currentMaxSpeed = this.nitroActive 
            ? this.nitroMaxSpeed 
            : this.maxSpeed;
        
        if (this.input.accelerate) {
            this.speed += currentAcceleration * deltaTime;
        }
        
        if (this.input.brake) {
            if (this.speed > 0) {
                this.speed -= this.brakeForce * deltaTime;
                this.speed = Math.max(0, this.speed);
            } else {
                this.speed -= this.accelerationForce * 0.5 * deltaTime;
            }
        }
        
        if (!this.input.accelerate && !this.input.brake) {
            this.speed *= this.friction;
            if (Math.abs(this.speed) < 0.1) {
                this.speed = 0;
            }
        }
        
        this.speed = Math.max(-this.maxSpeed * 0.3, Math.min(currentMaxSpeed, this.speed));
    }

    updateRotation(deltaTime) {
        let turnInput = 0;
        if (this.input.left) turnInput += 1;
        if (this.input.right) turnInput -= 1;
        
        const speedFactor = Math.min(Math.abs(this.speed) / 10, 1);
        const targetAngularVelocity = turnInput * this.turnSpeed * speedFactor;
        
        this.angularVelocity = THREE.MathUtils.lerp(
            this.angularVelocity,
            targetAngularVelocity,
            1 - this.inertia
        );
        
        this.heading += this.angularVelocity * deltaTime;
        
        const forward = new THREE.Vector3(
            Math.sin(this.heading),
            0,
            Math.cos(this.heading)
        );
        
        this.direction.lerp(forward, 0.3).normalize();
        
        if (turnInput !== 0 && Math.abs(this.speed) > 5) {
            this.slideAngle = this.angularVelocity * 0.1 * (this.speed / this.maxSpeed);
            this.isSliding = Math.abs(this.slideAngle) > 0.05;
        } else {
            this.slideAngle *= 0.9;
            this.isSliding = false;
        }
    }

    updateVelocity(deltaTime, currentTime) {
        const forwardVelocity = this.direction.clone().multiplyScalar(this.speed);
        
        const right = new THREE.Vector3(
            Math.cos(this.heading),
            0,
            -Math.sin(this.heading)
        );
        const lateralVelocity = right.multiplyScalar(this.speed * this.slideAngle * 0.5);
        
        this.velocity.lerp(forwardVelocity.add(lateralVelocity), 0.3);
        
        const trackInfo = this.getCachedTrackInfo(currentTime);
        if (trackInfo && this.track) {
            if (trackInfo.distance > this.track.trackWidth / 2 + 1) {
                this.velocity.multiplyScalar(0.98);
                this.speed *= 0.98;
            }
        }
    }

    updateVerticalPosition(deltaTime, currentTime) {
        const trackInfo = this.getCachedTrackInfo(currentTime);
        
        if (trackInfo && this.track && this.track.curve) {
            const trackHeight = trackInfo.point.y;
            
            const targetY = trackHeight + 0.5;
            this.position.y = THREE.MathUtils.lerp(this.position.y, targetY, 0.2);
            
            const tangent = this.track.getTrackTangentAt(trackInfo.t);
            const tiltAngle = Math.atan2(tangent.y, Math.sqrt(tangent.x * tangent.x + tangent.z * tangent.z));
            
            this.vehicle.rotation.x = tiltAngle * 0.5;
        }
        
        this.position.x += this.velocity.x * deltaTime;
        this.position.z += this.velocity.z * deltaTime;
    }

    updateVehicleTransform() {
        this.vehicle.position.copy(this.position);
        
        const lookTarget = this.position.clone().add(this.direction);
        this.vehicle.lookAt(lookTarget);
    }

    updateWheels(deltaTime) {
        const wheelRotation = this.speed * deltaTime * 0.5;
        
        this.wheels.forEach((wheel, index) => {
            if (index < 2) {
                wheel.rotation.x += wheelRotation;
                
                if (this.input.left) {
                    wheel.rotation.y = 0.3;
                } else if (this.input.right) {
                    wheel.rotation.y = -0.3;
                } else {
                    wheel.rotation.y = 0;
                }
            } else {
                wheel.rotation.x += wheelRotation;
            }
        });
    }

    applyCollision(collisionNormal, impactForce) {
        this.speed *= 0.3;
        
        const pushDirection = collisionNormal.clone().multiplyScalar(impactForce * 0.5);
        this.velocity.add(pushDirection);
        
        this.slideAngle = (Math.random() - 0.5) * 0.5;
        this.cachedTrackInfo = null;
    }

    getSpeed() {
        return Math.abs(this.speed);
    }

    getSpeedKmh() {
        return Math.abs(this.speed * 3.6);
    }

    getPosition() {
        return this.position.clone();
    }

    getDirection() {
        return this.direction.clone();
    }

    getBoundingBox() {
        const box = new THREE.Box3();
        box.setFromObject(this.vehicle);
        return box;
    }

    setInput(input) {
        Object.assign(this.input, input);
    }

    reset() {
        this.speed = 0;
        this.velocity.set(0, 0, 0);
        this.angularVelocity = 0;
        this.slideAngle = 0;
        this.cachedTrackInfo = null;
        this.nitroActive = false;
        this.nitroAmount = this.maxNitro;
        this.deactivateNitro();
    }
}
