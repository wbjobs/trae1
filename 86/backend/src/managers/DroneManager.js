const { v4: uuidv4 } = require('uuid');

class DroneManager {
  constructor(mqttClient) {
    this.drones = new Map();
    this.mqttClient = mqttClient;
    this.formationCenter = { lat: 31.2304, lng: 121.4737, alt: 0 };
  }

  initializeDrones(count = 5) {
    for (let i = 0; i < count; i++) {
      const droneId = `drone-${i + 1}`;
      this.drones.set(droneId, {
        id: droneId,
        name: `无人机 ${i + 1}`,
        connected: true,
        armed: false,
        locked: false,
        mode: 'GUIDED',
        position: {
          lat: this.formationCenter.lat + (Math.random() - 0.5) * 0.001,
          lng: this.formationCenter.lng + (Math.random() - 0.5) * 0.001,
          alt: 0
        },
        targetPosition: null,
        velocity: { x: 0, y: 0, z: 0 },
        battery: 100,
        gpsSatellites: 10 + Math.floor(Math.random() * 5),
        heading: 0,
        speed: 5,
        positionMode: 'gps',
        gpsLost: false,
        opticalFlow: { quality: 100, vx: 0, vy: 0 },
        imu: { ax: 0, ay: 0, az: 0, gx: 0, gy: 0, gz: 0 },
        lastUpdate: Date.now()
      });
    }
    console.log(`Initialized ${count} drones`);
  }

  getAllDrones() {
    return Array.from(this.drones.values());
  }

  getDrone(droneId) {
    return this.drones.get(droneId);
  }

  updateDroneTelemetry(droneId, data) {
    const drone = this.drones.get(droneId);
    if (drone) {
      drone.position = data.position || drone.position;
      drone.velocity = data.velocity || drone.velocity;
      drone.battery = data.battery ?? drone.battery;
      drone.gpsSatellites = data.gpsSatellites ?? drone.gpsSatellites;
      drone.heading = data.heading ?? drone.heading;
      drone.positionMode = data.positionMode || drone.positionMode;
      drone.gpsLost = data.gpsLost ?? drone.gpsLost;
      drone.opticalFlow = data.opticalFlow || drone.opticalFlow;
      drone.imu = data.imu || drone.imu;
      drone.lastUpdate = Date.now();
    }
  }

  updateDroneStatus(droneId, data) {
    const drone = this.drones.get(droneId);
    if (drone) {
      drone.connected = data.connected ?? drone.connected;
      drone.armed = data.armed ?? drone.armed;
      drone.mode = data.mode || drone.mode;
      drone.positionMode = data.positionMode || drone.positionMode;
      drone.gpsLost = data.gpsLost ?? drone.gpsLost;
      drone.speed = data.speed ?? drone.speed;
    }
  }

  lockDrone(droneId) {
    const drone = this.drones.get(droneId);
    if (drone) {
      drone.locked = true;
      this.publishCommand(droneId, 'lock');
    }
  }

  unlockDrone(droneId) {
    const drone = this.drones.get(droneId);
    if (drone) {
      drone.locked = false;
      this.publishCommand(droneId, 'unlock');
    }
  }

  manualControl(droneId, direction, speed = 1) {
    const drone = this.drones.get(droneId);
    if (!drone || drone.locked) return;

    const moveRate = 0.00001 * speed;
    const position = { ...drone.position };

    switch (direction) {
      case 'forward':
        position.lat += moveRate;
        break;
      case 'backward':
        position.lat -= moveRate;
        break;
      case 'left':
        position.lng -= moveRate;
        break;
      case 'right':
        position.lng += moveRate;
        break;
      case 'up':
        position.alt += 0.5 * speed;
        break;
      case 'down':
        position.alt -= 0.5 * speed;
        break;
    }

    this.setTargetPosition(droneId, position);
  }

  setTargetPosition(droneId, position) {
    const drone = this.drones.get(droneId);
    if (drone) {
      drone.targetPosition = position;
      this.publishCommand(droneId, 'goto', position);
    }
  }

  armDrone(droneId) {
    this.publishCommand(droneId, 'arm');
    const drone = this.drones.get(droneId);
    if (drone) drone.armed = true;
  }

  disarmDrone(droneId) {
    this.publishCommand(droneId, 'disarm');
    const drone = this.drones.get(droneId);
    if (drone) drone.armed = false;
  }

  takeoff(droneId, altitude) {
    this.publishCommand(droneId, 'takeoff', { altitude });
  }

  land(droneId) {
    this.publishCommand(droneId, 'land');
  }

  publishCommand(droneId, command, payload = {}) {
    const topic = `drones/${droneId}/command`;
    const message = JSON.stringify({ command, payload, timestamp: Date.now() });
    this.mqttClient.publish(topic, message);
  }

  setFormationCenter(center) {
    this.formationCenter = center;
  }

  getFormationCenter() {
    return this.formationCenter;
  }

  setSpeed(droneId, speed) {
    const drone = this.drones.get(droneId);
    if (drone) {
      drone.speed = Math.max(1, Math.min(10, speed));
      this.publishCommand(droneId, 'set_speed', { speed: drone.speed });
    }
  }

  simulateGPSLoss(droneId) {
    const drone = this.drones.get(droneId);
    if (drone) {
      this.publishCommand(droneId, 'simulate_gps_loss');
    }
  }

  simulateGPSRecovery(droneId) {
    const drone = this.drones.get(droneId);
    if (drone) {
      this.publishCommand(droneId, 'simulate_gps_recovery');
    }
  }
}

module.exports = DroneManager;