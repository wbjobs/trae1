const fs = require('fs');
const path = require('path');
const { v4: uuidv4 } = require('uuid');

class ObstacleLogger {
  constructor() {
    this.logsDir = path.join(__dirname, '../../logs/obstacle');
    this.currentSession = null;
    this.obstacleEvents = [];
    
    this.ensureLogsDir();
  }

  ensureLogsDir() {
    if (!fs.existsSync(this.logsDir)) {
      fs.mkdirSync(this.logsDir, { recursive: true });
    }
  }

  startSession(flightId) {
    this.currentSession = {
      id: uuidv4(),
      flightId,
      startTime: Date.now(),
      events: []
    };
    this.obstacleEvents = [];
    console.log(`Started obstacle logging session: ${this.currentSession.id}`);
  }

  logObstacleDetection(droneId, obstacle, distance, risk) {
    if (!this.currentSession) return;

    const event = {
      id: uuidv4(),
      type: 'detection',
      timestamp: Date.now(),
      droneId,
      obstacle: {
        id: obstacle.id,
        type: obstacle.type,
        position: { ...obstacle.position },
        velocity: { ...obstacle.velocity },
        radius: obstacle.radius
      },
      distance,
      risk: { ...risk }
    };

    this.obstacleEvents.push(event);
    this.currentSession.events.push(event);
  }

  logPathPlanning(start, goal, pathResult, formationType) {
    if (!this.currentSession) return;

    const event = {
      id: uuidv4(),
      type: 'path_planning',
      timestamp: Date.now(),
      start: { ...start },
      goal: { ...goal },
      planningTime: pathResult.planningTime,
      method: pathResult.method,
      success: pathResult.success,
      path: pathResult.success ? pathResult.path : null,
      formationType,
      error: pathResult.error || null
    };

    this.obstacleEvents.push(event);
    this.currentSession.events.push(event);
  }

  logFormationAction(action, reason, details = {}) {
    if (!this.currentSession) return;

    const event = {
      id: uuidv4(),
      type: 'formation_action',
      timestamp: Date.now(),
      action,
      reason,
      details
    };

    this.obstacleEvents.push(event);
    this.currentSession.events.push(event);
    console.log(`Formation action: ${action}, reason: ${reason}`);
  }

  logCollisionAvoidance(droneId, originalVelocity, newVelocity, computationTime) {
    if (!this.currentSession) return;

    const event = {
      id: uuidv4(),
      type: 'collision_avoidance',
      timestamp: Date.now(),
      droneId,
      originalVelocity: { ...originalVelocity },
      newVelocity: { ...newVelocity },
      computationTime
    };

    this.obstacleEvents.push(event);
    this.currentSession.events.push(event);
  }

  logNoFlyZoneViolation(droneId, position, zone) {
    if (!this.currentSession) return;

    const event = {
      id: uuidv4(),
      type: 'nfz_violation',
      timestamp: Date.now(),
      droneId,
      position: { ...position },
      zone: {
        id: zone.id,
        name: zone.name,
        type: zone.type
      }
    };

    this.obstacleEvents.push(event);
    this.currentSession.events.push(event);
    console.warn(`NFZ violation detected: ${droneId} in zone ${zone.name}`);
  }

  logDynamicObstacleUpdate(obstacle, predictedCollision) {
    if (!this.currentSession) return;

    const event = {
      id: uuidv4(),
      type: 'dynamic_obstacle',
      timestamp: Date.now(),
      obstacle: {
        id: obstacle.id,
        position: { ...obstacle.position },
        velocity: { ...obstacle.velocity }
      },
      predictedCollision
    };

    this.obstacleEvents.push(event);
    this.currentSession.events.push(event);
  }

  logFormationReassembly(dronesRealigned, timeTaken) {
    if (!this.currentSession) return;

    const event = {
      id: uuidv4(),
      type: 'formation_reassembly',
      timestamp: Date.now(),
      dronesRealigned,
      timeTaken
    };

    this.obstacleEvents.push(event);
    this.currentSession.events.push(event);
    console.log(`Formation reassembled: ${dronesRealigned} drones in ${timeTaken}ms`);
  }

  logGPSDegradation(droneId, fromMode, toMode, gpsSatellites) {
    if (!this.currentSession) return;

    const event = {
      id: uuidv4(),
      type: 'gps_degradation',
      timestamp: Date.now(),
      droneId,
      fromMode,
      toMode,
      gpsSatellites
    };

    this.obstacleEvents.push(event);
    this.currentSession.events.push(event);
    console.warn(`GPS degradation on ${droneId}: ${fromMode} -> ${toMode}, satellites: ${gpsSatellites}`);
  }

  endSession() {
    if (!this.currentSession) return;

    this.currentSession.endTime = Date.now();
    this.currentSession.duration = this.currentSession.endTime - this.currentSession.startTime;
    
    const sessionData = { ...this.currentSession };
    
    const filename = `obstacle_${this.currentSession.flightId || this.currentSession.id}.json`;
    const filepath = path.join(this.logsDir, filename);
    
    fs.writeFileSync(filepath, JSON.stringify(sessionData, null, 2));
    this.updateIndex(sessionData);
    
    console.log(`Obstacle log saved: ${filepath}`);
    
    const session = { ...this.currentSession };
    this.currentSession = null;
    this.obstacleEvents = [];
    
    return session;
  }

  updateIndex(sessionData) {
    const indexPath = path.join(this.logsDir, 'index.json');
    let index = [];
    
    if (fs.existsSync(indexPath)) {
      index = JSON.parse(fs.readFileSync(indexPath, 'utf8'));
    }
    
    const indexEntry = {
      id: sessionData.id,
      flightId: sessionData.flightId,
      startTime: new Date(sessionData.startTime).toISOString(),
      endTime: new Date(sessionData.endTime).toISOString(),
      duration: sessionData.duration,
      eventCount: sessionData.events.length
    };
    
    index.unshift(indexEntry);
    fs.writeFileSync(indexPath, JSON.stringify(index, null, 2));
  }

  getAvailableLogs() {
    const indexPath = path.join(this.logsDir, 'index.json');
    
    if (!fs.existsSync(indexPath)) {
      return [];
    }
    
    return JSON.parse(fs.readFileSync(indexPath, 'utf8'));
  }

  getLog(logId) {
    const filepath = path.join(this.logsDir, `obstacle_${logId}.json`);
    
    if (!fs.existsSync(filepath)) {
      return null;
    }
    
    return JSON.parse(fs.readFileSync(filepath, 'utf8'));
  }

  getCurrentEvents() {
    return [...this.obstacleEvents];
  }

  getEventCountByType() {
    const counts = {};
    
    for (const event of this.obstacleEvents) {
      counts[event.type] = (counts[event.type] || 0) + 1;
    }
    
    return counts;
  }
}

module.exports = ObstacleLogger;
