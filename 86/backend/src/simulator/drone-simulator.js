require('dotenv').config();
const mqtt = require('mqtt');

class DroneSimulator {
  constructor(droneId, initialPosition) {
    this.droneId = droneId;
    this.position = { ...initialPosition };
    this.truePosition = { ...initialPosition };
    this.targetPosition = null;
    this.velocity = { x: 0, y: 0, z: 0 };
    this.battery = 100;
    this.gpsSatellites = 10 + Math.floor(Math.random() * 5);
    this.heading = 0;
    this.armed = false;
    this.connected = true;
    this.speed = 5;
    this.baseSpeed = 5;
    
    this.positionMode = 'gps';
    this.gpsLost = false;
    this.gpsLossTimer = 0;
    this.gpsRecoveryTimer = 0;
    this.imuDrift = { x: 0, y: 0, z: 0 };
    this.opticalFlowData = { quality: 100, vx: 0, vy: 0 };
    this.imuData = { ax: 0, ay: 0, az: 0, gx: 0, gy: 0, gz: 0 };
    
    this.mqttClient = mqtt.connect(process.env.MQTT_BROKER_URL);
    this.setupMqtt();
  }

  setupMqtt() {
    this.mqttClient.on('connect', () => {
      console.log(`Drone ${this.droneId} connected to MQTT`);
      this.mqttClient.subscribe(`drones/${this.droneId}/command`);
      this.publishStatus();
    });

    this.mqttClient.on('message', (topic, message) => {
      if (topic.includes('command')) {
        const data = JSON.parse(message.toString());
        this.handleCommand(data);
      }
    });
  }

  handleCommand(data) {
    const { command, payload } = data;
    
    switch (command) {
      case 'arm':
        this.armed = true;
        this.publishStatus();
        break;
      
      case 'disarm':
        this.armed = false;
        this.publishStatus();
        break;
      
      case 'takeoff':
        this.armed = true;
        this.targetPosition = {
          ...this.position,
          alt: payload.altitude
        };
        this.publishStatus();
        break;
      
      case 'land':
        this.targetPosition = {
          ...this.position,
          alt: 0
        };
        break;
      
      case 'goto':
        if (this.armed) {
          this.targetPosition = { ...payload };
        }
        break;
      
      case 'lock':
        this.targetPosition = null;
        this.velocity = { x: 0, y: 0, z: 0 };
        break;
      
      case 'unlock':
        break;
      
      case 'set_speed':
        this.speed = payload.speed || this.baseSpeed;
        break;
      
      case 'simulate_gps_loss':
        this.simulateGpsLoss();
        break;
      
      case 'simulate_gps_recovery':
        this.simulateGpsRecovery();
        break;
    }
  }

  simulateGpsLoss() {
    if (!this.gpsLost) {
      this.gpsLost = true;
      this.gpsLossTimer = 0;
      this.positionMode = 'optical_flow';
      console.log(`${this.droneId}: GPS信号丢失，切换为视觉定位模式`);
      this.publishStatus();
    }
  }

  simulateGpsRecovery() {
    if (this.gpsLost) {
      this.gpsLost = false;
      this.gpsRecoveryTimer = 0;
      this.positionMode = 'gps';
      this.speed = this.baseSpeed;
      this.imuDrift = { x: 0, y: 0, z: 0 };
      console.log(`${this.droneId}: GPS信号恢复，切换为GPS定位模式`);
      this.publishStatus();
    }
  }

  update() {
    if (!this.armed) return;

    this.updateGpsStatus();
    this.updateSensorData();

    if (this.targetPosition) {
      this.moveToTarget();
    }

    if (this.battery > 0 && this.position.alt > 0) {
      this.battery -= 0.001;
    }

    this.publishTelemetry();
  }

  updateGpsStatus() {
    if (this.gpsLost) {
      this.gpsLossTimer++;
      this.gpsSatellites = Math.floor(Math.random() * 3);
      
      if (this.gpsLossTimer > 300 && Math.random() < 0.001) {
        this.positionMode = 'imu';
        console.log(`${this.droneId}: 视觉定位失效，切换为惯性导航模式`);
        this.publishStatus();
      }
    } else {
      this.gpsSatellites = 8 + Math.floor(Math.random() * 7);
      
      if (Math.random() < 0.0005) {
        this.simulateGpsLoss();
      }
    }
  }

  updateSensorData() {
    this.imuData = {
      ax: (Math.random() - 0.5) * 0.1,
      ay: (Math.random() - 0.5) * 0.1,
      az: 9.81 + (Math.random() - 0.5) * 0.1,
      gx: (Math.random() - 0.5) * 0.01,
      gy: (Math.random() - 0.5) * 0.01,
      gz: (Math.random() - 0.5) * 0.01
    };

    if (this.positionMode === 'optical_flow') {
      this.opticalFlowData = {
        quality: 60 + Math.floor(Math.random() * 30),
        vx: this.velocity.x,
        vy: this.velocity.y
      };
    } else if (this.positionMode === 'imu') {
      this.opticalFlowData.quality = 0;
      
      this.imuDrift.x += (Math.random() - 0.5) * 0.00001;
      this.imuDrift.y += (Math.random() - 0.5) * 0.00001;
      this.imuDrift.z += (Math.random() - 0.5) * 0.001;
    } else {
      this.opticalFlowData.quality = 100;
    }
  }

  moveToTarget() {
    const dx = this.targetPosition.lng - this.truePosition.lng;
    const dy = this.targetPosition.lat - this.truePosition.lat;
    const dz = this.targetPosition.alt - this.truePosition.alt;

    const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
    const moveThreshold = 0.000005;

    if (distance < moveThreshold) {
      this.truePosition = { ...this.targetPosition };
      this.velocity = { x: 0, y: 0, z: 0 };
    } else {
      const moveRate = (this.speed / 111000) * 0.1;
      const ratio = Math.min(moveRate / distance, 1);

      this.velocity = {
        x: dx * ratio * 10,
        y: dy * ratio * 10,
        z: dz * ratio * 10
      };

      this.truePosition.lng += dx * ratio;
      this.truePosition.lat += dy * ratio;
      this.truePosition.alt += dz * ratio;

      if (dx !== 0 || dy !== 0) {
        this.heading = Math.atan2(dy, dx) * 180 / Math.PI;
      }
    }

    this.position = this.calculateReportedPosition();
  }

  calculateReportedPosition() {
    const pos = { ...this.truePosition };
    
    if (this.positionMode === 'imu') {
      const driftFactor = 0.0001;
      pos.lng += this.imuDrift.x + (Math.random() - 0.5) * driftFactor;
      pos.lat += this.imuDrift.y + (Math.random() - 0.5) * driftFactor;
      pos.alt += this.imuDrift.z + (Math.random() - 0.5) * driftFactor * 10;
    } else if (this.positionMode === 'optical_flow') {
      const driftFactor = 0.00003;
      pos.lng += (Math.random() - 0.5) * driftFactor;
      pos.lat += (Math.random() - 0.5) * driftFactor;
      pos.alt += (Math.random() - 0.5) * driftFactor * 5;
    }
    
    return pos;
  }

  publishTelemetry() {
    const telemetry = {
      position: { ...this.position },
      velocity: { ...this.velocity },
      battery: Math.max(0, this.battery),
      gpsSatellites: this.gpsSatellites,
      heading: this.heading,
      positionMode: this.positionMode,
      gpsLost: this.gpsLost,
      opticalFlow: { ...this.opticalFlowData },
      imu: { ...this.imuData },
      timestamp: Date.now()
    };

    this.mqttClient.publish(
      `drones/${this.droneId}/telemetry`,
      JSON.stringify(telemetry)
    );
  }

  publishStatus() {
    const status = {
      connected: this.connected,
      armed: this.armed,
      mode: this.armed ? 'GUIDED' : 'STABILIZE',
      positionMode: this.positionMode,
      gpsLost: this.gpsLost,
      speed: this.speed,
      timestamp: Date.now()
    };

    this.mqttClient.publish(
      `drones/${this.droneId}/status`,
      JSON.stringify(status)
    );
  }

  disconnect() {
    this.connected = false;
    this.publishStatus();
    this.mqttClient.end();
  }
}

const baseLat = 31.2304;
const baseLng = 121.4737;
const drones = [];

for (let i = 0; i < 5; i++) {
  const drone = new DroneSimulator(`drone-${i + 1}`, {
    lat: baseLat + (Math.random() - 0.5) * 0.001,
    lng: baseLng + (Math.random() - 0.5) * 0.001,
    alt: 0
  });
  drones.push(drone);
}

setInterval(() => {
  drones.forEach(drone => drone.update());
}, parseInt(process.env.SIMULATOR_UPDATE_INTERVAL) || 100);

console.log('Drone simulator running with 5 drones');

process.on('SIGINT', () => {
  console.log('Shutting down drone simulator...');
  drones.forEach(drone => drone.disconnect());
  process.exit(0);
});