const fs = require('fs');
const path = require('path');
const { v4: uuidv4 } = require('uuid');

class FlightLogger {
  constructor() {
    this.logsDir = path.join(__dirname, '../../logs');
    this.currentLog = null;
    this.isRecording = false;
    
    this.ensureLogsDir();
  }

  ensureLogsDir() {
    if (!fs.existsSync(this.logsDir)) {
      fs.mkdirSync(this.logsDir, { recursive: true });
    }
  }

  startRecording() {
    if (this.isRecording) return;
    
    const logId = uuidv4();
    const startTime = new Date().toISOString();
    
    this.currentLog = {
      id: logId,
      startTime,
      endTime: null,
      telemetry: []
    };
    
    this.isRecording = true;
    console.log(`Started recording flight log: ${logId}`);
  }

  stopRecording() {
    if (!this.isRecording || !this.currentLog) return;
    
    this.currentLog.endTime = new Date().toISOString();
    this.saveLog(this.currentLog);
    
    this.isRecording = false;
    console.log(`Stopped recording flight log: ${this.currentLog.id}`);
  }

  logTelemetry(droneId, data) {
    if (!this.isRecording || !this.currentLog) return;
    
    this.currentLog.telemetry.push({
      timestamp: Date.now(),
      droneId,
      position: data.position,
      velocity: data.velocity,
      battery: data.battery,
      gpsSatellites: data.gpsSatellites,
      heading: data.heading
    });
  }

  saveLog(log) {
    const filename = `flight_${log.id}.json`;
    const filepath = path.join(this.logsDir, filename);
    
    const logInfo = {
      id: log.id,
      startTime: log.startTime,
      endTime: log.endTime,
      duration: this.calculateDuration(log.startTime, log.endTime)
    };
    
    fs.writeFileSync(filepath, JSON.stringify(log, null, 2));
    this.updateLogIndex(logInfo);
  }

  calculateDuration(startTime, endTime) {
    const start = new Date(startTime).getTime();
    const end = new Date(endTime).getTime();
    return Math.floor((end - start) / 1000);
  }

  updateLogIndex(logInfo) {
    const indexPath = path.join(this.logsDir, 'index.json');
    let index = [];
    
    if (fs.existsSync(indexPath)) {
      index = JSON.parse(fs.readFileSync(indexPath, 'utf8'));
    }
    
    index.unshift(logInfo);
    fs.writeFileSync(indexPath, JSON.stringify(index, null, 2));
  }

  getAvailableLogs() {
    const indexPath = path.join(this.logsDir, 'index.json');
    
    if (!fs.existsSync(indexPath)) {
      return [];
    }
    
    return JSON.parse(fs.readFileSync(indexPath, 'utf8'));
  }

  loadLog(logId) {
    const filepath = path.join(this.logsDir, `flight_${logId}.json`);
    
    if (!fs.existsSync(filepath)) {
      return null;
    }
    
    return JSON.parse(fs.readFileSync(filepath, 'utf8'));
  }

  deleteLog(logId) {
    const filepath = path.join(this.logsDir, `flight_${logId}.json`);
    const indexPath = path.join(this.logsDir, 'index.json');
    
    if (fs.existsSync(filepath)) {
      fs.unlinkSync(filepath);
    }
    
    if (fs.existsSync(indexPath)) {
      let index = JSON.parse(fs.readFileSync(indexPath, 'utf8'));
      index = index.filter(log => log.id !== logId);
      fs.writeFileSync(indexPath, JSON.stringify(index, null, 2));
    }
  }

  getCurrentLogId() {
    return this.currentLog ? this.currentLog.id : null;
  }
}

module.exports = FlightLogger;