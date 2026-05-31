class CameraController {
    constructor(camera, vehicle) {
        this.camera = camera;
        this.vehicle = vehicle;
        
        this.cameraModes = [
            { name: '第三人称', index: 0 },
            { name: '第一人称', index: 1 },
            { name: '车顶视角', index: 2 },
            { name: '追逐视角', index: 3 },
            { name: '空中俯视', index: 4 }
        ];
        
        this.currentMode = 0;
        this.cameraOffset = new THREE.Vector3(0, 5, -10);
        this.lookAtOffset = new THREE.Vector3(0, 1, 0);
        
        this.smoothness = 0.1;
        this.currentPosition = new THREE.Vector3();
        this.currentLookAt = new THREE.Vector3();
        
        this.shakeIntensity = 0;
        this.shakeDecay = 0.9;
        
        this.cameraModeNames = ['第三人称', '第一人称', '车顶视角', '追逐视角', '空中俯视'];
    }

    update(deltaTime) {
        if (!this.vehicle) return;
        
        const vehiclePosition = this.vehicle.getPosition();
        const vehicleDirection = this.vehicle.getDirection();
        const vehicleSpeed = this.vehicle.getSpeed();
        
        let targetPosition = new THREE.Vector3();
        let targetLookAt = new THREE.Vector3();
        
        switch (this.currentMode) {
            case 0:
                targetPosition = this.getThirdPersonView(vehiclePosition, vehicleDirection, vehicleSpeed);
                targetLookAt = vehiclePosition.clone().add(this.lookAtOffset);
                break;
            case 1:
                targetPosition = this.getFirstPersonView(vehiclePosition, vehicleDirection);
                targetLookAt = vehiclePosition.clone().add(vehicleDirection.clone().multiplyScalar(10)).add(new THREE.Vector3(0, 1, 0));
                break;
            case 2:
                targetPosition = this.getRoofView(vehiclePosition, vehicleDirection);
                targetLookAt = vehiclePosition.clone().add(vehicleDirection.clone().multiplyScalar(5));
                break;
            case 3:
                targetPosition = this.getChaseView(vehiclePosition, vehicleDirection, vehicleSpeed);
                targetLookAt = vehiclePosition.clone().add(this.lookAtOffset);
                break;
            case 4:
                targetPosition = this.getTopDownView(vehiclePosition);
                targetLookAt = vehiclePosition;
                break;
        }
        
        this.applyShake(targetPosition);
        
        this.currentPosition.lerp(targetPosition, this.smoothness);
        this.currentLookAt.lerp(targetLookAt, this.smoothness);
        
        this.camera.position.copy(this.currentPosition);
        this.camera.lookAt(this.currentLookAt);
        
        this.updateCameraIndicator();
    }

    getThirdPersonView(vehiclePosition, vehicleDirection, speed) {
        const backDirection = vehicleDirection.clone().negate();
        const up = new THREE.Vector3(0, 1, 0);
        
        const distance = 8 + Math.min(speed * 0.1, 5);
        const height = 4 + speed * 0.05;
        
        const offset = backDirection.multiplyScalar(distance).add(up.multiplyScalar(height));
        
        return vehiclePosition.clone().add(offset);
    }

    getFirstPersonView(vehiclePosition, vehicleDirection) {
        const forward = vehicleDirection.clone();
        const up = new THREE.Vector3(0, 1, 0);
        
        const offset = forward.multiplyScalar(0.5).add(up.multiplyScalar(1.2));
        
        return vehiclePosition.clone().add(offset);
    }

    getRoofView(vehiclePosition, vehicleDirection) {
        const up = new THREE.Vector3(0, 1, 0);
        const offset = up.multiplyScalar(3);
        
        return vehiclePosition.clone().add(offset);
    }

    getChaseView(vehiclePosition, vehicleDirection, speed) {
        const backDirection = vehicleDirection.clone().negate();
        const up = new THREE.Vector3(0, 1, 0);
        
        const distance = 4;
        const height = 2;
        
        const offset = backDirection.multiplyScalar(distance).add(up.multiplyScalar(height));
        
        return vehiclePosition.clone().add(offset);
    }

    getTopDownView(vehiclePosition) {
        const up = new THREE.Vector3(0, 1, 0);
        const offset = up.multiplyScalar(30);
        
        return vehiclePosition.clone().add(offset);
    }

    applyShake(position) {
        if (this.shakeIntensity > 0) {
            position.x += (Math.random() - 0.5) * this.shakeIntensity;
            position.y += (Math.random() - 0.5) * this.shakeIntensity;
            position.z += (Math.random() - 0.5) * this.shakeIntensity;
            
            this.shakeIntensity *= this.shakeDecay;
            
            if (this.shakeIntensity < 0.01) {
                this.shakeIntensity = 0;
            }
        }
    }

    triggerShake(intensity = 0.5) {
        this.shakeIntensity = Math.max(this.shakeIntensity, intensity);
    }

    switchCameraMode() {
        this.currentMode = (this.currentMode + 1) % this.cameraModes.length;
        this.currentPosition.copy(this.camera.position);
    }

    setCameraMode(mode) {
        if (mode >= 0 && mode < this.cameraModes.length) {
            this.currentMode = mode;
            this.currentPosition.copy(this.camera.position);
        }
    }

    getCurrentModeName() {
        return this.cameraModeNames[this.currentMode];
    }

    updateCameraIndicator() {
        const indicator = document.getElementById('camera-mode');
        if (indicator) {
            indicator.textContent = this.getCurrentModeName();
        }
    }

    setSmoothness(value) {
        this.smoothness = Math.max(0.01, Math.min(1, value));
    }

    reset() {
        this.currentMode = 0;
        this.shakeIntensity = 0;
        
        if (this.vehicle) {
            const vehiclePosition = this.vehicle.getPosition();
            const targetPosition = this.getThirdPersonView(
                vehiclePosition,
                this.vehicle.getDirection(),
                0
            );
            this.currentPosition.copy(targetPosition);
            this.currentLookAt.copy(vehiclePosition);
            this.camera.position.copy(targetPosition);
            this.camera.lookAt(vehiclePosition);
        }
    }
}
