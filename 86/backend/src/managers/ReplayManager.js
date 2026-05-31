class ReplayManager {
  constructor(io) {
    this.io = io;
    this.currentLog = null;
    this.isPlaying = false;
    this.isPaused = false;
    this.currentTime = 0;
    this.playbackInterval = null;
    this.playbackSpeed = 1;
  }

  startReplay(logData) {
    if (!logData || !logData.telemetry || logData.telemetry.length === 0) {
      return false;
    }

    this.stopReplay();
    this.currentLog = logData;
    this.currentTime = 0;
    this.isPlaying = true;
    this.isPaused = false;

    const startTime = logData.telemetry[0].timestamp;
    const endTime = logData.telemetry[logData.telemetry.length - 1].timestamp;
    const duration = endTime - startTime;

    this.io.emit('replay:started', {
      logId: logData.id,
      startTime,
      endTime,
      duration
    });

    this.playbackInterval = setInterval(() => {
      if (this.isPaused) return;
      
      this.currentTime += 100 * this.playbackSpeed;
      this.sendTelemetryAtTime(startTime + this.currentTime);
      
      if (this.currentTime >= duration) {
        this.stopReplay();
        this.io.emit('replay:finished');
      }
    }, 100);

    return true;
  }

  sendTelemetryAtTime(targetTime) {
    if (!this.currentLog) return;

    const telemetry = this.currentLog.telemetry;
    let closestEntry = telemetry[0];
    
    for (const entry of telemetry) {
      if (entry.timestamp <= targetTime) {
        closestEntry = entry;
      } else {
        break;
      }
    }

    this.io.emit('drone:telemetry', {
      droneId: closestEntry.droneId,
      position: closestEntry.position,
      velocity: closestEntry.velocity,
      battery: closestEntry.battery,
      gpsSatellites: closestEntry.gpsSatellites,
      heading: closestEntry.heading
    });

    this.io.emit('replay:progress', {
      currentTime: this.currentTime,
      timestamp: closestEntry.timestamp
    });
  }

  pauseReplay() {
    this.isPaused = true;
    this.io.emit('replay:paused');
  }

  resumeReplay() {
    this.isPaused = false;
    this.io.emit('replay:resumed');
  }

  stopReplay() {
    this.isPlaying = false;
    this.isPaused = false;
    
    if (this.playbackInterval) {
      clearInterval(this.playbackInterval);
      this.playbackInterval = null;
    }
    
    this.io.emit('replay:stopped');
  }

  seekReplay(timestamp) {
    if (!this.currentLog) return;
    
    const startTime = this.currentLog.telemetry[0].timestamp;
    this.currentTime = timestamp - startTime;
    this.sendTelemetryAtTime(timestamp);
  }

  setPlaybackSpeed(speed) {
    this.playbackSpeed = Math.max(0.25, Math.min(4, speed));
    this.io.emit('replay:speed_changed', { speed: this.playbackSpeed });
  }

  getPlaybackState() {
    return {
      isPlaying: this.isPlaying,
      isPaused: this.isPaused,
      currentTime: this.currentTime,
      playbackSpeed: this.playbackSpeed,
      logId: this.currentLog ? this.currentLog.id : null
    };
  }
}

module.exports = ReplayManager;