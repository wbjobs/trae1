class TrackGenerator {
    constructor(scene) {
        this.scene = scene;
        this.trackPoints = [];
        this.trackWidth = 8;
        this.trackMesh = null;
        this.barriers = [];
        this.obstacles = [];
        this.checkpoints = [];
        this.startLine = null;
        this.trackLength = 0;
        this.curve = null;
        
        this.obstacleRefreshInterval = 10;
        this.lastObstacleRefresh = 0;
        this.nitroPickups = [];
    }

    generateRandomTrack(seed = Math.random()) {
        this.clearTrack();
        
        const rng = this.createSeededRandom(seed);
        const numPoints = 12 + Math.floor(rng() * 8);
        const radius = 80 + rng() * 40;
        
        for (let i = 0; i < numPoints; i++) {
            const angle = (i / numPoints) * Math.PI * 2;
            const variation = (rng() - 0.5) * 40;
            const r = radius + variation;
            const x = Math.cos(angle) * r;
            const z = Math.sin(angle) * r;
            const y = (rng() - 0.5) * 20;
            
            this.trackPoints.push(new THREE.Vector3(x, y, z));
        }
        
        this.curve = new THREE.CatmullRomCurve3(this.trackPoints, true, 'catmullrom', 0.5);
        
        const curvePoints = this.curve.getPoints(500);
        this.trackLength = this.curve.getLength();
        
        this.createTrackSurface(curvePoints);
        this.createBarriers(curvePoints);
        this.createObstacles(rng);
        this.createNitroPickups(rng);
        this.createCheckpoints();
        this.createStartLine();
        this.createEnvironment();
        
        return this.trackPoints;
    }

    createSeededRandom(seed) {
        let s = seed;
        return function() {
            s = (s * 9301 + 49297) % 233280;
            return s / 233280;
        };
    }

    createTrackSurface(curvePoints) {
        const trackShape = new THREE.Shape();
        trackShape.moveTo(-this.trackWidth / 2, 0);
        trackShape.lineTo(this.trackWidth / 2, 0);
        trackShape.lineTo(this.trackWidth / 2, 0.5);
        trackShape.lineTo(-this.trackWidth / 2, 0.5);
        trackShape.lineTo(-this.trackWidth / 2, 0);

        const extrudeSettings = {
            steps: 500,
            extrudePath: this.curve
        };

        const trackGeometry = new THREE.ExtrudeGeometry(trackShape, extrudeSettings);
        const trackMaterial = new THREE.MeshStandardMaterial({
            color: 0x333333,
            roughness: 0.8,
            metalness: 0.2,
            side: THREE.DoubleSide
        });

        this.trackMesh = new THREE.Mesh(trackGeometry, trackMaterial);
        this.trackMesh.receiveShadow = true;
        this.scene.add(this.trackMesh);

        this.createTrackLines(curvePoints);
    }

    createTrackLines(curvePoints) {
        const lineMaterial = new THREE.LineBasicMaterial({ color: 0xffff00, linewidth: 2 });
        const leftLinePoints = [];
        const rightLinePoints = [];
        
        for (let i = 0; i < curvePoints.length; i++) {
            const point = curvePoints[i];
            const tangent = this.curve.getTangentAt(i / curvePoints.length).normalize();
            const normal = new THREE.Vector3(-tangent.z, 0, tangent.x).normalize();
            
            leftLinePoints.push(point.clone().add(normal.multiplyScalar(this.trackWidth / 2 - 0.5)));
            rightLinePoints.push(point.clone().add(normal.multiplyScalar(-this.trackWidth / 2 + 0.5)));
        }

        const leftLineGeometry = new THREE.BufferGeometry().setFromPoints(leftLinePoints);
        const rightLineGeometry = new THREE.BufferGeometry().setFromPoints(rightLinePoints);
        
        const leftLine = new THREE.Line(leftLineGeometry, lineMaterial);
        const rightLine = new THREE.Line(rightLineGeometry, lineMaterial);
        
        this.scene.add(leftLine);
        this.scene.add(rightLine);
    }

    createBarriers(curvePoints) {
        const barrierGeometry = new THREE.BoxGeometry(0.3, 1.5, 1.5);
        const barrierMaterial = new THREE.MeshStandardMaterial({
            color: 0xff3333,
            roughness: 0.5,
            metalness: 0.3
        });

        const barrierSpacing = 5;
        for (let i = 0; i < curvePoints.length; i += barrierSpacing) {
            const point = curvePoints[i];
            const tangent = this.curve.getTangentAt(i / curvePoints.length).normalize();
            const normal = new THREE.Vector3(-tangent.z, 0, tangent.x).normalize();
            
            const leftBarrier = new THREE.Mesh(barrierGeometry, barrierMaterial);
            leftBarrier.position.copy(point).add(normal.clone().multiplyScalar(this.trackWidth / 2 + 0.5));
            leftBarrier.position.y += 0.75;
            leftBarrier.castShadow = true;
            this.scene.add(leftBarrier);
            this.barriers.push(leftBarrier);

            const rightBarrier = new THREE.Mesh(barrierGeometry, barrierMaterial);
            rightBarrier.position.copy(point).add(normal.clone().multiplyScalar(-this.trackWidth / 2 - 0.5));
            rightBarrier.position.y += 0.75;
            rightBarrier.castShadow = true;
            this.scene.add(rightBarrier);
            this.barriers.push(rightBarrier);
        }
    }

    createObstacles(rng) {
        const obstacleCount = 20 + Math.floor(rng() * 15);
        
        for (let i = 0; i < obstacleCount; i++) {
            const t = rng();
            const position = this.curve.getPointAt(t);
            const tangent = this.curve.getTangentAt(t).normalize();
            const normal = new THREE.Vector3(-tangent.z, 0, tangent.x).normalize();
            
            const offset = (rng() - 0.5) * (this.trackWidth - 2);
            const obstacleType = Math.floor(rng() * 3);
            
            let obstacle;
            if (obstacleType === 0) {
                const geom = new THREE.BoxGeometry(1.5, 1, 1.5);
                const mat = new THREE.MeshStandardMaterial({
                    color: 0xff6600,
                    roughness: 0.6,
                    metalness: 0.2
                });
                obstacle = new THREE.Mesh(geom, mat);
            } else if (obstacleType === 1) {
                const geom = new THREE.ConeGeometry(0.8, 1.2, 6);
                const mat = new THREE.MeshStandardMaterial({
                    color: 0xffcc00,
                    roughness: 0.5,
                    metalness: 0.3
                });
                obstacle = new THREE.Mesh(geom, mat);
            } else {
                const geom = new THREE.TorusGeometry(0.6, 0.2, 8, 16);
                const mat = new THREE.MeshStandardMaterial({
                    color: 0xff00ff,
                    roughness: 0.4,
                    metalness: 0.6
                });
                obstacle = new THREE.Mesh(geom, mat);
            }
            
            obstacle.position.copy(position).add(normal.multiplyScalar(offset));
            obstacle.position.y += 0.5;
            obstacle.castShadow = true;
            obstacle.userData = { type: 'obstacle', active: true };
            
            this.scene.add(obstacle);
            this.obstacles.push(obstacle);
        }
    }

    createNitroPickups(rng) {
        const nitroCount = 5 + Math.floor(rng() * 5);
        
        for (let i = 0; i < nitroCount; i++) {
            const t = rng();
            const position = this.curve.getPointAt(t);
            const tangent = this.curve.getTangentAt(t).normalize();
            const normal = new THREE.Vector3(-tangent.z, 0, tangent.x).normalize();
            
            const offset = (rng() - 0.5) * (this.trackWidth - 3);
            
            const nitroGroup = new THREE.Group();
            
            const baseGeometry = new THREE.CylinderGeometry(0.4, 0.4, 0.1, 8);
            const baseMaterial = new THREE.MeshStandardMaterial({ color: 0x444444 });
            const base = new THREE.Mesh(baseGeometry, baseMaterial);
            base.position.y = 0.05;
            nitroGroup.add(base);
            
            const tankGeometry = new THREE.CylinderGeometry(0.25, 0.3, 0.8, 8);
            const tankMaterial = new THREE.MeshStandardMaterial({
                color: 0x00ffff,
                emissive: 0x00ffff,
                emissiveIntensity: 0.5,
                transparent: true,
                opacity: 0.8
            });
            const tank = new THREE.Mesh(tankGeometry, tankMaterial);
            tank.position.y = 0.5;
            nitroGroup.add(tank);
            
            const capGeometry = new THREE.CylinderGeometry(0.3, 0.25, 0.1, 8);
            const capMaterial = new THREE.MeshStandardMaterial({ color: 0x008888 });
            const cap = new THREE.Mesh(capGeometry, capMaterial);
            cap.position.y = 0.95;
            nitroGroup.add(cap);
            
            nitroGroup.position.copy(position).add(normal.multiplyScalar(offset));
            nitroGroup.position.y += 0.1;
            nitroGroup.userData = { type: 'nitro', active: true, floatOffset: rng() * Math.PI * 2 };
            
            this.scene.add(nitroGroup);
            this.nitroPickups.push(nitroGroup);
        }
    }

    refreshObstacles(currentTime) {
        if (currentTime - this.lastObstacleRefresh < this.obstacleRefreshInterval) return;
        
        this.lastObstacleRefresh = currentTime;
        
        const inactiveObstacles = this.obstacles.filter(o => !o.userData.active);
        
        if (inactiveObstacles.length > 0 && Math.random() > 0.5) {
            const obstacle = inactiveObstacles[Math.floor(Math.random() * inactiveObstacles.length)];
            const t = Math.random();
            const position = this.curve.getPointAt(t);
            const tangent = this.curve.getTangentAt(t).normalize();
            const normal = new THREE.Vector3(-tangent.z, 0, tangent.x).normalize();
            const offset = (Math.random() - 0.5) * (this.trackWidth - 2);
            
            obstacle.position.copy(position).add(normal.multiplyScalar(offset));
            obstacle.position.y += 0.5;
            obstacle.userData.active = true;
            obstacle.visible = true;
        }
        
        const inactiveNitros = this.nitroPickups.filter(n => !n.userData.active);
        if (inactiveNitros.length > 0 && Math.random() > 0.7) {
            const nitro = inactiveNitros[Math.floor(Math.random() * inactiveNitros.length)];
            const t = Math.random();
            const position = this.curve.getPointAt(t);
            const tangent = this.curve.getTangentAt(t).normalize();
            const normal = new THREE.Vector3(-tangent.z, 0, tangent.x).normalize();
            const offset = (Math.random() - 0.5) * (this.trackWidth - 3);
            
            nitro.position.copy(position).add(normal.multiplyScalar(offset));
            nitro.position.y += 0.1;
            nitro.userData.active = true;
            nitro.visible = true;
        }
    }

    updateNitroPickups(currentTime) {
        this.nitroPickups.forEach(nitro => {
            if (nitro.userData.active) {
                nitro.rotation.y += 0.02;
                nitro.position.y = nitro.position.y + Math.sin(currentTime * 3 + nitro.userData.floatOffset) * 0.001;
            }
        });
    }

    createCheckpoints() {
        const numCheckpoints = 8;
        const checkpointGeometry = new THREE.TorusGeometry(5, 0.3, 8, 32);
        const checkpointMaterial = new THREE.MeshStandardMaterial({
            color: 0x00ff00,
            emissive: 0x00ff00,
            emissiveIntensity: 0.3,
            transparent: true,
            opacity: 0.6
        });

        for (let i = 0; i < numCheckpoints; i++) {
            const t = (i + 1) / (numCheckpoints + 1);
            const position = this.curve.getPointAt(t);
            const tangent = this.curve.getTangentAt(t).normalize();
            
            const checkpoint = new THREE.Mesh(checkpointGeometry, checkpointMaterial);
            checkpoint.position.copy(position);
            checkpoint.position.y += 3;
            checkpoint.lookAt(position.clone().add(tangent));
            checkpoint.rotateX(Math.PI / 2);
            checkpoint.userData = { type: 'checkpoint', index: i, passed: false };
            
            this.scene.add(checkpoint);
            this.checkpoints.push(checkpoint);
        }
    }

    createStartLine() {
        const startPosition = this.curve.getPointAt(0);
        const startTangent = this.curve.getTangentAt(0).normalize();
        
        const startLineGeometry = new THREE.PlaneGeometry(this.trackWidth, 0.5);
        const startLineMaterial = new THREE.MeshStandardMaterial({
            color: 0xffffff,
            side: THREE.DoubleSide,
            emissive: 0xffffff,
            emissiveIntensity: 0.2
        });
        
        this.startLine = new THREE.Mesh(startLineGeometry, startLineMaterial);
        this.startLine.position.copy(startPosition);
        this.startLine.position.y += 0.02;
        this.startLine.lookAt(startPosition.clone().add(startTangent));
        this.startLine.rotateX(-Math.PI / 2);
        this.scene.add(this.startLine);

        const bannerGeometry = new THREE.BoxGeometry(this.trackWidth + 4, 6, 0.3);
        const bannerMaterial = new THREE.MeshStandardMaterial({
            color: 0xff0000,
            emissive: 0xff0000,
            emissiveIntensity: 0.2
        });
        
        const banner = new THREE.Mesh(bannerGeometry, bannerMaterial);
        banner.position.copy(startPosition);
        banner.position.y += 5;
        this.scene.add(banner);
    }

    createEnvironment() {
        const groundGeometry = new THREE.PlaneGeometry(500, 500);
        const groundMaterial = new THREE.MeshStandardMaterial({
            color: 0x1a472a,
            roughness: 0.9
        });
        const ground = new THREE.Mesh(groundGeometry, groundMaterial);
        ground.rotation.x = -Math.PI / 2;
        ground.position.y = -10;
        ground.receiveShadow = true;
        this.scene.add(ground);

        for (let i = 0; i < 50; i++) {
            const treeType = Math.floor(Math.random() * 3);
            let tree;
            
            if (treeType === 0) {
                const trunkGeom = new THREE.CylinderGeometry(0.3, 0.5, 4, 8);
                const trunkMat = new THREE.MeshStandardMaterial({ color: 0x4a3728 });
                const trunk = new THREE.Mesh(trunkGeom, trunkMat);
                
                const leavesGeom = new THREE.ConeGeometry(2, 6, 8);
                const leavesMat = new THREE.MeshStandardMaterial({ color: 0x228b22 });
                const leaves = new THREE.Mesh(leavesGeom, leavesMat);
                leaves.position.y = 5;
                
                tree = new THREE.Group();
                tree.add(trunk);
                tree.add(leaves);
            } else if (treeType === 1) {
                const rockGeom = new THREE.DodecahedronGeometry(1 + Math.random() * 2);
                const rockMat = new THREE.MeshStandardMaterial({ color: 0x696969 });
                tree = new THREE.Mesh(rockGeom, rockMat);
            } else {
                const buildingGeom = new THREE.BoxGeometry(
                    3 + Math.random() * 5,
                    10 + Math.random() * 20,
                    3 + Math.random() * 5
                );
                const buildingMat = new THREE.MeshStandardMaterial({ color: 0x444455 });
                tree = new THREE.Mesh(buildingGeom, buildingMat);
            }
            
            const angle = Math.random() * Math.PI * 2;
            const distance = 100 + Math.random() * 150;
            tree.position.set(
                Math.cos(angle) * distance,
                treeType === 0 ? 0 : (treeType === 1 ? Math.random() * 3 : 5),
                Math.sin(angle) * distance
            );
            tree.castShadow = true;
            this.scene.add(tree);
        }
    }

    clearTrack() {
        if (this.trackMesh) {
            this.scene.remove(this.trackMesh);
        }
        
        this.barriers.forEach(barrier => this.scene.remove(barrier));
        this.obstacles.forEach(obstacle => this.scene.remove(obstacle));
        this.checkpoints.forEach(checkpoint => this.scene.remove(checkpoint));
        this.nitroPickups.forEach(nitro => this.scene.remove(nitro));
        if (this.startLine) {
            this.scene.remove(this.startLine);
        }
        
        this.trackPoints = [];
        this.barriers = [];
        this.obstacles = [];
        this.checkpoints = [];
        this.nitroPickups = [];
        this.trackMesh = null;
        this.startLine = null;
        this.lastObstacleRefresh = 0;
    }

    getTrackPointAt(t) {
        return this.curve.getPointAt(t);
    }

    getTrackTangentAt(t) {
        return this.curve.getTangentAt(t).normalize();
    }

    getClosestTrackPoint(position) {
        let minDistance = Infinity;
        let closestT = 0;
        let closestPoint = null;

        for (let t = 0; t <= 1; t += 0.01) {
            const point = this.curve.getPointAt(t);
            const distance = position.distanceTo(point);
            
            if (distance < minDistance) {
                minDistance = distance;
                closestT = t;
                closestPoint = point;
            }
        }

        const refineStart = Math.max(0, closestT - 0.01);
        const refineEnd = Math.min(1, closestT + 0.01);
        
        for (let t = refineStart; t <= refineEnd; t += 0.001) {
            const point = this.curve.getPointAt(t);
            const distance = position.distanceTo(point);
            
            if (distance < minDistance) {
                minDistance = distance;
                closestT = t;
                closestPoint = point;
            }
        }

        return { t: closestT, point: closestPoint, distance: minDistance };
    }

    checkNitroPickup(vehiclePosition) {
        for (const nitro of this.nitroPickups) {
            if (!nitro.userData.active) continue;
            
            const distance = vehiclePosition.distanceTo(nitro.position);
            if (distance < 2) {
                nitro.userData.active = false;
                nitro.visible = false;
                return true;
            }
        }
        return false;
    }

    getStartPosition() {
        const position = this.curve.getPointAt(0);
        const tangent = this.curve.getTangentAt(0).normalize();
        const normal = new THREE.Vector3(-tangent.z, 0, tangent.x).normalize();
        
        return {
            position: position.clone().add(normal.multiplyScalar(0)),
            direction: tangent
        };
    }

    renderMinimap(ctx, canvasWidth, canvasHeight, carPosition) {
        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
        
        ctx.fillStyle = 'rgba(0, 30, 0, 0.8)';
        ctx.fillRect(0, 0, canvasWidth, canvasHeight);
        
        const points = this.curve.getPoints(200);
        const padding = 20;
        
        let minX = Infinity, maxX = -Infinity;
        let minZ = Infinity, maxZ = -Infinity;
        
        points.forEach(p => {
            minX = Math.min(minX, p.x);
            maxX = Math.max(maxX, p.x);
            minZ = Math.min(minZ, p.z);
            maxZ = Math.max(maxZ, p.z);
        });
        
        const scaleX = (canvasWidth - 2 * padding) / (maxX - minX);
        const scaleZ = (canvasHeight - 2 * padding) / (maxZ - minZ);
        const scale = Math.min(scaleX, scaleZ);
        
        const offsetX = (canvasWidth - (maxX - minX) * scale) / 2;
        const offsetZ = (canvasHeight - (maxZ - minZ) * scale) / 2;
        
        ctx.beginPath();
        ctx.strokeStyle = '#444';
        ctx.lineWidth = 6;
        
        points.forEach((p, i) => {
            const x = (p.x - minX) * scale + offsetX;
            const y = (p.z - minZ) * scale + offsetZ;
            
            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        });
        
        ctx.closePath();
        ctx.stroke();
        
        ctx.strokeStyle = '#0ff';
        ctx.lineWidth = 2;
        ctx.stroke();
        
        const startPos = this.getStartPosition().position;
        const startX = (startPos.x - minX) * scale + offsetX;
        const startY = (startPos.z - minZ) * scale + offsetZ;
        
        ctx.fillStyle = '#0f0';
        ctx.beginPath();
        ctx.arc(startX, startY, 5, 0, Math.PI * 2);
        ctx.fill();
        
        if (carPosition) {
            const carX = (carPosition.x - minX) * scale + offsetX;
            const carY = (carPosition.z - minZ) * scale + offsetZ;
            
            ctx.fillStyle = '#f00';
            ctx.beginPath();
            ctx.arc(carX, carY, 6, 0, Math.PI * 2);
            ctx.fill();
            
            ctx.fillStyle = '#fff';
            ctx.beginPath();
            ctx.arc(carX, carY, 3, 0, Math.PI * 2);
            ctx.fill();
        }
        
        this.checkpoints.forEach((checkpoint, index) => {
            if (checkpoint.userData.passed) {
                const cpX = (checkpoint.position.x - minX) * scale + offsetX;
                const cpY = (checkpoint.position.z - minZ) * scale + offsetZ;
                
                ctx.fillStyle = '#ff0';
                ctx.beginPath();
                ctx.arc(cpX, cpY, 4, 0, Math.PI * 2);
                ctx.fill();
            }
        });
    }
}
