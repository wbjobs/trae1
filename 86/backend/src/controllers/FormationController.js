const AStarPathPlanner = require('./PathPlanner');
const VelocityObstacle = require('./VelocityObstacle');
const NoFlyZoneManager = require('../managers/NoFlyZoneManager');
const ObstacleLogger = require('../utils/ObstacleLogger');

class FormationController {
  constructor(droneManager, mqttClient, io) {
    this.droneManager = droneManager;
    this.mqttClient = mqttClient;
    this.io = io;
    
    this.currentFormation = {
      type: 'line',
      center: { lat: 31.2304, lng: 121.4737, alt: 10 },
      spacing: 10,
      originalSpacing: 10,
      positions: []
    };
    
    this.maintenanceInterval = null;
    this.obstacleInterval = null;
    this.tolerance = parseFloat(process.env.FORMATION_TOLERANCE) || 0.5;
    
    this.noFlyZoneManager = new NoFlyZoneManager();
    this.pathPlanner = new AStarPathPlanner(this.noFlyZoneManager);
    this.velocityObstacle = new VelocityObstacle();
    this.obstacleLogger = new ObstacleLogger();
    
    this.state = {
      isAvoiding: false,
      isFormationBroken: false,
      avoidReason: null,
      gpsDegradedDrones: new Set(),
      formationSpeedFactor: 1.0,
      plannedPath: null,
      currentWaypointIndex: 0,
      reassemblyStartTime: null
    };
    
    this.setupInitialNoFlyZones();
  }

  setupInitialNoFlyZones() {
    const baseLat = 31.2304;
    const baseLng = 121.4737;
    
    this.noFlyZoneManager.addZone({
      type: 'circle',
      name: '建筑物禁飞区',
      center: { lat: baseLat + 0.0005, lng: baseLng + 0.0008, alt: 0 },
      radius: 20,
      minAlt: 0,
      maxAlt: 50
    });
    
    this.noFlyZoneManager.addZone({
      type: 'polygon',
      name: '机场禁飞区',
      coordinates: [
        { lat: baseLat + 0.001, lng: baseLng - 0.001 },
        { lat: baseLat + 0.002, lng: baseLng - 0.0005 },
        { lat: baseLat + 0.0015, lng: baseLng + 0.0005 },
        { lat: baseLat + 0.0005, lng: baseLng }
      ],
      minAlt: 0,
      maxAlt: 100
    });
  }

  setFormation(type, center, spacing = 10) {
    this.currentFormation.type = type;
    this.currentFormation.center = center || this.currentFormation.center;
    this.currentFormation.spacing = spacing;
    this.currentFormation.originalSpacing = spacing;
    this.currentFormation.positions = this.calculateFormationPositions(type, this.currentFormation.center, spacing);
    
    this.applyFormation();
    this.startFormationMaintenance();
    
    return this.getCurrentFormation();
  }

  calculateFormationPositions(type, center, spacing) {
    const drones = this.droneManager.getAllDrones();
    const count = drones.length;
    const positions = [];

    const latOffset = (meters) => meters / 111000;
    const lngOffset = (meters, lat) => meters / (111000 * Math.cos(lat * Math.PI / 180));

    switch (type) {
      case 'line':
        for (let i = 0; i < count; i++) {
          const offset = (i - (count - 1) / 2) * spacing;
          positions.push({
            lat: center.lat,
            lng: center.lng + lngOffset(offset, center.lat),
            alt: center.alt
          });
        }
        break;

      case 'v_shape':
        const angle = Math.PI / 4;
        for (let i = 0; i < count; i++) {
          if (i === 0) {
            positions.push({ ...center });
          } else {
            const side = i % 2 === 0 ? 1 : -1;
            const row = Math.floor((i + 1) / 2);
            positions.push({
              lat: center.lat + latOffset(row * spacing * Math.cos(angle)),
              lng: center.lng + lngOffset(side * row * spacing * Math.sin(angle), center.lat),
              alt: center.alt
            });
          }
        }
        break;

      case 'circle':
        const radius = spacing * count / (2 * Math.PI);
        for (let i = 0; i < count; i++) {
          const angle = (2 * Math.PI * i) / count;
          positions.push({
            lat: center.lat + latOffset(radius * Math.cos(angle)),
            lng: center.lng + lngOffset(radius * Math.sin(angle), center.lat),
            alt: center.alt
          });
        }
        break;

      case 'triangle':
        let row = 0, col = 0, rowCount = 1;
        for (let i = 0; i < count; i++) {
          positions.push({
            lat: center.lat + latOffset(row * spacing * 0.866),
            lng: center.lng + lngOffset((col - (rowCount - 1) / 2) * spacing, center.lat),
            alt: center.alt
          });
          col++;
          if (col >= rowCount) {
            row++;
            col = 0;
            rowCount++;
          }
        }
        break;
    }

    return positions;
  }

  applyFormation() {
    const drones = this.droneManager.getAllDrones();
    drones.forEach((drone, index) => {
      if (!drone.locked && index < this.currentFormation.positions.length) {
        const target = this.currentFormation.positions[index];
        const adjustedTarget = this.adjustTargetForNoFlyZones(drone, target);
        this.droneManager.setTargetPosition(drone.id, adjustedTarget);
      }
    });
  }

  adjustTargetForNoFlyZones(drone, target) {
    const collision = this.noFlyZoneManager.checkCollision(target, 2);
    if (!collision) return target;

    const start = drone.position || this.currentFormation.center;
    const result = this.pathPlanner.planPath(start, target, 3);
    
    if (result.success && result.path.length > 1) {
      return result.path[1];
    }
    
    const dx = target.lng - start.lng;
    const dy = target.lat - start.lat;
    const dist = Math.sqrt(dx * dx + dy * dy);
    return {
      lat: start.lat + (dy / dist) * 0.0001,
      lng: start.lng + (dx / dist) * 0.0001,
      alt: target.alt + 5
    };
  }

  startFormationMaintenance() {
    if (this.maintenanceInterval) {
      clearInterval(this.maintenanceInterval);
    }
    if (this.obstacleInterval) {
      clearInterval(this.obstacleInterval);
    }

    this.maintenanceInterval = setInterval(() => {
      this.maintainFormation();
      this.checkGPSStatus();
    }, 500);

    this.obstacleInterval = setInterval(() => {
      this.checkObstaclesAndAvoid();
    }, 100);
  }

  maintainFormation() {
    const drones = this.droneManager.getAllDrones();
    
    drones.forEach((drone, index) => {
      if (drone.locked || !drone.armed) return;
      
      let target = this.currentFormation.positions[index];
      if (!target) return;

      if (this.state.isAvoiding && this.state.isFormationBroken) {
        const dynamicObstacles = this.noFlyZoneManager.getDynamicObstacles();
        const voResult = this.velocityObstacle.computeAvoidanceVelocity(
          drone.position,
          drone.velocity,
          dynamicObstacles,
          this.calculateDesiredVelocity(drone, target)
        );

        if (voResult.avoidanceNeeded) {
          const newTarget = this.applyVelocityToPosition(drone.position, voResult.velocity);
          this.droneManager.setTargetPosition(drone.id, newTarget);
          
          this.obstacleLogger.logCollisionAvoidance(
            drone.id,
            drone.velocity,
            voResult.velocity,
            voResult.computationTime
          );
          
          if (voResult.computationTime > 100) {
            console.warn(`Slow collision avoidance on ${drone.id}: ${voResult.computationTime}ms`);
          }
          return;
        }
      }

      const distance = this.calculateDistance(drone.position, target);
      const effectiveTolerance = this.state.gpsDegradedDrones.has(drone.id) 
        ? this.tolerance * 2 
        : this.tolerance;
      
      if (distance > effectiveTolerance) {
        const adjustedTarget = this.state.gpsDegradedDrones.has(drone.id)
          ? this.applySlowdown(target, drone)
          : target;
        
        this.droneManager.setTargetPosition(drone.id, adjustedTarget);
      }
    });

    if (!this.state.isAvoiding && this.state.isFormationBroken) {
      this.tryReassembleFormation();
    }
  }

  checkGPSStatus() {
    const drones = this.droneManager.getAllDrones();
    let anyDegraded = false;
    
    drones.forEach(drone => {
      if (drone.gpsLost || drone.positionMode !== 'gps') {
        if (!this.state.gpsDegradedDrones.has(drone.id)) {
          this.state.gpsDegradedDrones.add(drone.id);
          this.obstacleLogger.logGPSDegradation(
            drone.id,
            'gps',
            drone.positionMode,
            drone.gpsSatellites
          );
          console.log(`GPS degraded on ${drone.id}: ${drone.positionMode}`);
        }
        anyDegraded = true;
      } else {
        if (this.state.gpsDegradedDrones.has(drone.id)) {
          this.state.gpsDegradedDrones.delete(drone.id);
          console.log(`GPS recovered on ${drone.id}`);
        }
      }
    });

    if (anyDegraded && !this.state.isAvoiding) {
      this.activateGPSDegradationMode();
    } else if (!anyDegraded && this.state.isAvoiding && this.state.avoidReason === 'gps_degradation') {
      this.deactivateGPSDegradationMode();
    }
  }

  activateGPSDegradationMode() {
    if (this.state.isAvoiding) return;
    
    this.state.isAvoiding = true;
    this.state.avoidReason = 'gps_degradation';
    this.state.formationSpeedFactor = 0.5;
    this.currentFormation.spacing = this.currentFormation.originalSpacing * 1.5;
    
    const drones = this.droneManager.getAllDrones();
    drones.forEach(drone => {
      if (!drone.locked) {
        this.droneManager.setSpeed(drone.id, drone.speed * 0.5);
      }
    });
    
    this.obstacleLogger.logFormationAction(
      'gps_degradation_activated',
      'GPS信号丢失，切换降级模式',
      { affectedDrones: Array.from(this.state.gpsDegradedDrones) }
    );
    
    this.recalculateFormation();
    this.io.emit('formation:degraded', {
      reason: 'gps_degradation',
      affectedDrones: Array.from(this.state.gpsDegradedDrones),
      speedFactor: 0.5,
      spacingMultiplier: 1.5
    });
  }

  deactivateGPSDegradationMode() {
    this.state.isAvoiding = false;
    this.state.avoidReason = null;
    this.state.formationSpeedFactor = 1.0;
    this.currentFormation.spacing = this.currentFormation.originalSpacing;
    
    const drones = this.droneManager.getAllDrones();
    drones.forEach(drone => {
      if (!drone.locked) {
        this.droneManager.setSpeed(drone.id, 5);
      }
    });
    
    this.obstacleLogger.logFormationAction(
      'gps_degradation_deactivated',
      'GPS信号恢复，恢复正常模式'
    );
    
    this.recalculateFormation();
    this.io.emit('formation:restored', {
      speedFactor: 1.0,
      spacingMultiplier: 1.0
    });
  }

  checkObstaclesAndAvoid() {
    const startTime = Date.now();
    const drones = this.droneManager.getAllDrones();
    const dynamicObstacles = this.noFlyZoneManager.getDynamicObstacles();
    
    let collisionRiskDetected = false;
    let nfzCollisionDetected = false;
    
    for (const drone of drones) {
      if (drone.locked || !drone.armed || !drone.position) continue;
      
      const nfzCollision = this.noFlyZoneManager.checkCollision(drone.position, 1);
      if (nfzCollision) {
        nfzCollisionDetected = true;
        this.obstacleLogger.logNoFlyZoneViolation(drone.id, drone.position, nfzCollision);
      }
      
      if (dynamicObstacles.length > 0) {
        const risk = this.velocityObstacle.assessCollisionRisk(drone.position, dynamicObstacles);
        
        if (risk.level === 'high') {
          collisionRiskDetected = true;
          this.obstacleLogger.logObstacleDetection(
            drone.id,
            risk.nearestObstacle,
            risk.nearestDistance,
            risk
          );
        }
      }
    }
    
    if (collisionRiskDetected || nfzCollisionDetected) {
      if (!this.state.isFormationBroken) {
        this.breakFormationForAvoidance(collisionRiskDetected ? 'dynamic_obstacle' : 'nfz_violation');
      }
    } else if (this.state.isFormationBroken && this.state.avoidReason !== 'gps_degradation') {
      if (!this.state.reassemblyStartTime) {
        this.state.reassemblyStartTime = Date.now();
      } else if (Date.now() - this.state.reassemblyStartTime > 3000) {
        this.reassembleFormation();
      }
    }
    
    const totalTime = Date.now() - startTime;
    if (totalTime > 100) {
      console.warn(`Slow obstacle check: ${totalTime}ms`);
    }
  }

  breakFormationForAvoidance(reason) {
    this.state.isAvoiding = true;
    this.state.isFormationBroken = true;
    this.state.avoidReason = reason;
    this.state.reassemblyStartTime = null;
    
    this.obstacleLogger.logFormationAction(
      'formation_broken',
      `编队解散进行避障: ${reason}`,
      { reason }
    );
    
    this.io.emit('formation:broken', {
      reason,
      timestamp: Date.now()
    });
    
    console.log(`Formation broken for avoidance: ${reason}`);
  }

  tryReassembleFormation() {
    const drones = this.droneManager.getAllDrones();
    let allInPosition = true;
    
    drones.forEach((drone, index) => {
      if (drone.locked || !drone.armed) return;
      
      const target = this.currentFormation.positions[index];
      if (!target) return;
      
      const distance = this.calculateDistance(drone.position, target);
      if (distance > this.tolerance * 3) {
        allInPosition = false;
      }
    });
    
    if (allInPosition) {
      this.reassembleFormation();
    }
  }

  reassembleFormation() {
    const startTime = Date.now();
    
    this.state.isAvoiding = false;
    this.state.isFormationBroken = false;
    this.state.avoidReason = null;
    this.state.reassemblyStartTime = null;
    this.state.formationSpeedFactor = 1.0;
    this.currentFormation.spacing = this.currentFormation.originalSpacing;
    
    const drones = this.droneManager.getAllDrones();
    drones.forEach(drone => {
      if (!drone.locked) {
        this.droneManager.setSpeed(drone.id, 5);
      }
    });
    
    this.recalculateFormation();
    
    const timeTaken = Date.now() - startTime;
    this.obstacleLogger.logFormationReassembly(drones.filter(d => !d.locked).length, timeTaken);
    
    this.io.emit('formation:reassembled', {
      timeTaken,
      dronesRealigned: drones.filter(d => !d.locked).length
    });
    
    console.log(`Formation reassembled in ${timeTaken}ms`);
  }

  recalculateFormation() {
    this.currentFormation.positions = this.calculateFormationPositions(
      this.currentFormation.type,
      this.currentFormation.center,
      this.currentFormation.spacing
    );
    this.applyFormation();
  }

  calculateDesiredVelocity(drone, target) {
    const dx = target.lng - drone.position.lng;
    const dy = target.lat - drone.position.lat;
    const dz = target.alt - drone.position.alt;
    
    const distance = this.calculateDistance(drone.position, target);
    if (distance < 0.1) return { x: 0, y: 0, z: 0 };
    
    const speed = 5 * this.state.formationSpeedFactor;
    const ratio = speed / distance;
    
    return {
      x: dx * ratio * 111000 * Math.cos(drone.position.lat * Math.PI / 180),
      y: dy * ratio * 111000,
      z: dz * ratio
    };
  }

  applyVelocityToPosition(position, velocity) {
    return {
      lat: position.lat + velocity.y / 111000,
      lng: position.lng + velocity.x / (111000 * Math.cos(position.lat * Math.PI / 180)),
      alt: position.alt + velocity.z
    };
  }

  applySlowdown(target, drone) {
    const center = this.currentFormation.center;
    return {
      lat: drone.position.lat + (target.lat - drone.position.lat) * 0.5,
      lng: drone.position.lng + (target.lng - drone.position.lng) * 0.5,
      alt: drone.position.alt + (target.alt - drone.position.alt) * 0.5
    };
  }

  calculateDistance(pos1, pos2) {
    const dx = (pos2.lng - pos1.lng) * 111000 * Math.cos(pos1.lat * Math.PI / 180);
    const dy = (pos2.lat - pos1.lat) * 111000;
    const dz = (pos2.alt || 0) - (pos1.alt || 0);
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
  }

  async planPathToGoal(goal) {
    const start = this.currentFormation.center;
    const result = this.pathPlanner.planFormationPath(
      start,
      goal,
      this.currentFormation.type,
      this.currentFormation.spacing
    );
    
    this.obstacleLogger.logPathPlanning(
      start,
      goal,
      result,
      this.currentFormation.type
    );
    
    if (result.success) {
      this.state.plannedPath = result.path;
      this.state.currentWaypointIndex = 0;
      
      this.io.emit('path:planned', {
        path: result.path,
        formationPath: result.formationPath,
        planningTime: result.planningTime,
        method: result.method
      });
    }
    
    return result;
  }

  async takeoffAll(altitude = 10) {
    const drones = this.droneManager.getAllDrones();
    
    this.obstacleLogger.startSession();
    
    for (const drone of drones) {
      if (!drone.locked) {
        this.droneManager.armDrone(drone.id);
        await this.sleep(100);
        this.droneManager.takeoff(drone.id, altitude);
      }
    }

    await this.sleep(3000);
    this.currentFormation.center.alt = altitude;
    this.setFormation(this.currentFormation.type, this.currentFormation.center, this.currentFormation.spacing);
  }

  async landAll() {
    const drones = this.droneManager.getAllDrones();
    
    for (const drone of drones) {
      if (!drone.locked) {
        this.droneManager.land(drone.id);
      }
    }

    if (this.maintenanceInterval) {
      clearInterval(this.maintenanceInterval);
      this.maintenanceInterval = null;
    }
    
    if (this.obstacleInterval) {
      clearInterval(this.obstacleInterval);
      this.obstacleInterval = null;
    }
    
    this.obstacleLogger.endSession();
    
    this.state.isAvoiding = false;
    this.state.isFormationBroken = false;
    this.state.gpsDegradedDrones.clear();
  }

  moveFormation(vector) {
    const { dx = 0, dy = 0, dz = 0 } = vector;
    const factor = this.state.formationSpeedFactor;
    
    this.currentFormation.center.lat += (dy * factor) / 111000;
    this.currentFormation.center.lng += (dx * factor) / (111000 * Math.cos(this.currentFormation.center.lat * Math.PI / 180));
    this.currentFormation.center.alt += dz * factor;

    const collision = this.noFlyZoneManager.checkCollision(this.currentFormation.center, 3);
    if (collision) {
      const result = this.planPathToGoal(this.currentFormation.center);
      if (result.success && result.path.length > 1) {
        const nextWaypoint = result.path[1];
        this.currentFormation.center = nextWaypoint;
      }
    }

    this.recalculateFormation();
  }

  addNoFlyZone(zone) {
    const newZone = this.noFlyZoneManager.addZone(zone);
    this.io.emit('nfz:added', newZone);
    return newZone;
  }

  removeNoFlyZone(zoneId) {
    const result = this.noFlyZoneManager.removeZone(zoneId);
    if (result) {
      this.io.emit('nfz:removed', zoneId);
    }
    return result;
  }

  addDynamicObstacle(obstacle) {
    const newObstacle = this.noFlyZoneManager.addDynamicObstacle(obstacle);
    this.io.emit('obstacle:added', newObstacle);
    return newObstacle;
  }

  getNoFlyZones() {
    return this.noFlyZoneManager.getZones();
  }

  getDynamicObstacles() {
    return this.noFlyZoneManager.getDynamicObstacles();
  }

  getCurrentFormation() {
    return {
      ...this.currentFormation,
      state: { ...this.state, gpsDegradedDrones: Array.from(this.state.gpsDegradedDrones) },
      noFlyZones: this.getNoFlyZones(),
      dynamicObstacles: this.getDynamicObstacles(),
      plannedPath: this.state.plannedPath
    };
  }

  getObstacleLogs() {
    return this.obstacleLogger.getAvailableLogs();
  }

  getObstacleLog(logId) {
    return this.obstacleLogger.getLog(logId);
  }

  getCurrentObstacleEvents() {
    return this.obstacleLogger.getCurrentEvents();
  }

  simulateGPSLoss(droneId) {
    this.droneManager.simulateGPSLoss(droneId);
  }

  simulateGPSRecovery(droneId) {
    this.droneManager.simulateGPSRecovery(droneId);
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = FormationController;
