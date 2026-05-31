class ReplayManager {
    constructor() {
        this.isRecording = false;
        this.isPlaying = false;
        this.recordedData = [];
        this.currentPlaybackIndex = 0;
        this.playbackStartTime = 0;
        this.recordInterval = 0.02;
        this.lastRecordTime = 0;
        this.maxRecordSize = 100000;
        
        this.savedReplays = [];
        this.loadSavedReplays();
        
        this.playbackSpeed = 1;
    }

    startRecording() {
        this.recordedData = [];
        this.isRecording = true;
        this.isPlaying = false;
        this.lastRecordTime = performance.now();
        this.currentPlaybackIndex = 0;
    }

    stopRecording() {
        this.isRecording = false;
        return this.recordedData.length > 0;
    }

    recordFrame(vehicleData) {
        if (!this.isRecording) return;
        
        const currentTime = performance.now();
        const deltaTime = (currentTime - this.lastRecordTime) / 1000;
        
        if (deltaTime < this.recordInterval) return;
        
        if (this.recordedData.length >= this.maxRecordSize) {
            this.stopRecording();
            return;
        }
        
        this.recordedData.push({
            timestamp: currentTime,
            position: {
                x: vehicleData.position.x,
                y: vehicleData.position.y,
                z: vehicleData.position.z
            },
            heading: vehicleData.heading,
            speed: vehicleData.speed,
            nitroAmount: vehicleData.nitroAmount,
            nitroActive: vehicleData.nitroActive
        });
        
        this.lastRecordTime = currentTime;
    }

    startPlayback() {
        if (this.recordedData.length === 0) return false;
        
        this.isPlaying = true;
        this.isRecording = false;
        this.currentPlaybackIndex = 0;
        this.playbackStartTime = performance.now();
        
        return true;
    }

    stopPlayback() {
        this.isPlaying = false;
        this.currentPlaybackIndex = 0;
    }

    getPlaybackFrame() {
        if (!this.isPlaying || this.recordedData.length === 0) return null;
        
        const elapsedTime = (performance.now() - this.playbackStartTime) * this.playbackSpeed;
        const targetTime = this.recordedData[0].timestamp + elapsedTime;
        
        while (this.currentPlaybackIndex < this.recordedData.length - 1) {
            const nextFrame = this.recordedData[this.currentPlaybackIndex + 1];
            if (nextFrame.timestamp > targetTime) break;
            this.currentPlaybackIndex++;
        }
        
        if (this.currentPlaybackIndex >= this.recordedData.length - 1) {
            this.stopPlayback();
            return null;
        }
        
        const currentFrame = this.recordedData[this.currentPlaybackIndex];
        const nextFrame = this.recordedData[this.currentPlaybackIndex + 1];
        
        const frameDelta = nextFrame.timestamp - currentFrame.timestamp;
        const interpolation = (targetTime - currentFrame.timestamp) / frameDelta;
        
        return {
            position: {
                x: this.lerp(currentFrame.position.x, nextFrame.position.x, interpolation),
                y: this.lerp(currentFrame.position.y, nextFrame.position.y, interpolation),
                z: this.lerp(currentFrame.position.z, nextFrame.position.z, interpolation)
            },
            heading: this.lerp(currentFrame.heading, nextFrame.heading, interpolation),
            speed: this.lerp(currentFrame.speed, nextFrame.speed, interpolation),
            nitroAmount: this.lerp(currentFrame.nitroAmount, nextFrame.nitroAmount, interpolation),
            nitroActive: nextFrame.nitroActive
        };
    }

    lerp(a, b, t) {
        return a + (b - a) * t;
    }

    saveCurrentReplay(name) {
        if (this.recordedData.length === 0) return false;
        
        const replay = {
            name: name || `竞速录像 ${new Date().toLocaleString('zh-CN')}`,
            date: Date.now(),
            duration: this.recordedData.length > 0 
                ? this.recordedData[this.recordedData.length - 1].timestamp - this.recordedData[0].timestamp
                : 0,
            frameCount: this.recordedData.length,
            data: this.recordedData
        };
        
        this.savedReplays.push(replay);
        this.saveReplaysToStorage();
        
        return true;
    }

    loadReplay(index) {
        if (index < 0 || index >= this.savedReplays.length) return false;
        
        const replay = this.savedReplays[index];
        this.recordedData = replay.data;
        return true;
    }

    deleteReplay(index) {
        if (index < 0 || index >= this.savedReplays.length) return false;
        
        this.savedReplays.splice(index, 1);
        this.saveReplaysToStorage();
        return true;
    }

    getSavedReplays() {
        return this.savedReplays.map((replay, index) => ({
            index,
            name: replay.name,
            date: new Date(replay.date).toLocaleString('zh-CN'),
            duration: Math.floor(replay.duration / 1000),
            frameCount: replay.frameCount
        }));
    }

    saveReplaysToStorage() {
        try {
            const toSave = this.savedReplays.slice(-5);
            
            localStorage.setItem('racingReplays', JSON.stringify(toSave));
        } catch (e) {
            console.log('Could not save replays:', e);
        }
    }

    loadSavedReplays() {
        try {
            const saved = localStorage.getItem('racingReplays');
            if (saved) {
                this.savedReplays = JSON.parse(saved);
            }
        } catch (e) {
            this.savedReplays = [];
        }
    }

    getRecordedDuration() {
        if (this.recordedData.length < 2) return 0;
        
        const startTime = this.recordedData[0].timestamp;
        const endTime = this.recordedData[this.recordedData.length - 1].timestamp;
        
        return Math.floor((endTime - startTime) / 1000);
    }

    getRecordingStatus() {
        return {
            isRecording: this.isRecording,
            isPlaying: this.isPlaying,
            frameCount: this.recordedData.length,
            duration: this.getRecordedDuration(),
            savedCount: this.savedReplays.length
        };
    }
}
