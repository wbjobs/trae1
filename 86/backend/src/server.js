require('dotenv').config();
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const mqtt = require('mqtt');
const cors = require('cors');

const FormationController = require('./controllers/FormationController');
const DroneManager = require('./managers/DroneManager');
const FlightLogger = require('./utils/FlightLogger');
const ReplayManager = require('./managers/ReplayManager');

const app = express();
app.use(cors());
app.use(express.json());

const server = http.createServer(app);
const io = new Server(server, {
  cors: {
    origin: "*",
    methods: ["GET", "POST"]
  }
});

const mqttClient = mqtt.connect(process.env.MQTT_BROKER_URL);
const droneManager = new DroneManager(mqttClient);
const flightLogger = new FlightLogger();
const replayManager = new ReplayManager(io);

let formationController;

mqttClient.on('connect', () => {
  console.log('Connected to MQTT Broker');
  mqttClient.subscribe('drones/+/telemetry');
  mqttClient.subscribe('drones/+/status');
  
  if (!formationController) {
    formationController = new FormationController(droneManager, mqttClient, io);
    console.log('FormationController initialized with obstacle avoidance');
  }
});

mqttClient.on('message', (topic, message) => {
  const data = JSON.parse(message.toString());
  const droneId = topic.split('/')[1];
  
  if (topic.includes('telemetry')) {
    droneManager.updateDroneTelemetry(droneId, data);
    flightLogger.logTelemetry(droneId, data);
    io.emit('drone:telemetry', { droneId, ...data });
  } else if (topic.includes('status')) {
    droneManager.updateDroneStatus(droneId, data);
    io.emit('drone:status', { droneId, ...data });
  }
});

io.on('connection', (socket) => {
  console.log('Client connected:', socket.id);
  
  socket.emit('drones:list', droneManager.getAllDrones());
  if (formationController) {
    socket.emit('formation:current', formationController.getCurrentFormation());
    socket.emit('nfz:list', formationController.getNoFlyZones());
    socket.emit('obstacle:list', formationController.getDynamicObstacles());
  }
  
  socket.on('formation:set', (data) => {
    if (!formationController) return;
    formationController.setFormation(data.type, data.center, data.spacing);
    io.emit('formation:current', formationController.getCurrentFormation());
  });
  
  socket.on('formation:takeoff', async (altitude) => {
    if (!formationController) return;
    await formationController.takeoffAll(altitude || 10);
    flightLogger.startRecording();
    io.emit('formation:status', { action: 'takeoff', status: 'completed' });
  });
  
  socket.on('formation:land', async () => {
    if (!formationController) return;
    await formationController.landAll();
    flightLogger.stopRecording();
    io.emit('formation:status', { action: 'land', status: 'completed' });
  });
  
  socket.on('formation:move', (vector) => {
    if (!formationController) return;
    formationController.moveFormation(vector);
  });

  socket.on('formation:planPath', async (goal) => {
    if (!formationController) return;
    const result = await formationController.planPathToGoal(goal);
    socket.emit('path:result', result);
  });
  
  socket.on('drone:lock', (droneId) => {
    droneManager.lockDrone(droneId);
    io.emit('drone:locked', { droneId, locked: true });
  });
  
  socket.on('drone:unlock', (droneId) => {
    droneManager.unlockDrone(droneId);
    io.emit('drone:locked', { droneId, locked: false });
  });
  
  socket.on('drone:manual', (data) => {
    const { droneId, direction, speed } = data;
    droneManager.manualControl(droneId, direction, speed);
  });

  socket.on('drone:simulateGpsLoss', (droneId) => {
    if (!formationController) return;
    formationController.simulateGPSLoss(droneId);
  });

  socket.on('drone:simulateGpsRecovery', (droneId) => {
    if (!formationController) return;
    formationController.simulateGPSRecovery(droneId);
  });
  
  socket.on('nfz:add', (zone) => {
    if (!formationController) return;
    const newZone = formationController.addNoFlyZone(zone);
    io.emit('nfz:list', formationController.getNoFlyZones());
    socket.emit('nfz:added', newZone);
  });

  socket.on('nfz:remove', (zoneId) => {
    if (!formationController) return;
    formationController.removeNoFlyZone(zoneId);
    io.emit('nfz:list', formationController.getNoFlyZones());
  });

  socket.on('nfz:get', () => {
    if (!formationController) return;
    socket.emit('nfz:list', formationController.getNoFlyZones());
  });

  socket.on('obstacle:add', (obstacle) => {
    if (!formationController) return;
    const newObstacle = formationController.addDynamicObstacle(obstacle);
    io.emit('obstacle:list', formationController.getDynamicObstacles());
    socket.emit('obstacle:added', newObstacle);
  });

  socket.on('obstacle:simulate', (count = 2) => {
    if (!formationController) return;
    const baseLat = 31.2304;
    const baseLng = 121.4737;
    
    for (let i = 0; i < count; i++) {
      formationController.addDynamicObstacle({
        id: `intruder-${i + 1}`,
        type: 'drone_swarm',
        name: `入侵无人机群 ${i + 1}`,
        position: {
          lat: baseLat + 0.0003 + Math.random() * 0.0004,
          lng: baseLng + 0.0003 + Math.random() * 0.0004,
          alt: 10 + Math.random() * 5
        },
        velocity: {
          x: (Math.random() - 0.5) * 3,
          y: (Math.random() - 0.5) * 3,
          z: (Math.random() - 0.5) * 0.5
        },
        radius: 3 + Math.random() * 2
      });
    }
  });
  
  socket.on('logs:get', () => {
    socket.emit('logs:list', flightLogger.getAvailableLogs());
  });
  
  socket.on('logs:load', (logId) => {
    const logData = flightLogger.loadLog(logId);
    socket.emit('logs:data', logData);
  });

  socket.on('obstacleLogs:get', () => {
    if (!formationController) return;
    socket.emit('obstacleLogs:list', formationController.getObstacleLogs());
  });

  socket.on('obstacleLogs:load', (logId) => {
    if (!formationController) return;
    const logData = formationController.getObstacleLog(logId);
    socket.emit('obstacleLogs:data', logData);
  });

  socket.on('obstacleEvents:get', () => {
    if (!formationController) return;
    socket.emit('obstacleEvents:current', formationController.getCurrentObstacleEvents());
  });
  
  socket.on('replay:start', (logId) => {
    const logData = flightLogger.loadLog(logId);
    replayManager.startReplay(logData);
  });
  
  socket.on('replay:pause', () => {
    replayManager.pauseReplay();
  });
  
  socket.on('replay:stop', () => {
    replayManager.stopReplay();
  });
  
  socket.on('replay:seek', (timestamp) => {
    replayManager.seekReplay(timestamp);
  });
  
  socket.on('disconnect', () => {
    console.log('Client disconnected:', socket.id);
  });
});

app.get('/api/drones', (req, res) => {
  res.json(droneManager.getAllDrones());
});

app.get('/api/formation', (req, res) => {
  if (!formationController) {
    return res.status(503).json({ error: 'FormationController not initialized' });
  }
  res.json(formationController.getCurrentFormation());
});

app.get('/api/logs', (req, res) => {
  res.json(flightLogger.getAvailableLogs());
});

app.get('/api/logs/:id', (req, res) => {
  const logData = flightLogger.loadLog(req.params.id);
  res.json(logData);
});

app.get('/api/nfz', (req, res) => {
  if (!formationController) {
    return res.status(503).json({ error: 'FormationController not initialized' });
  }
  res.json(formationController.getNoFlyZones());
});

app.post('/api/nfz', (req, res) => {
  if (!formationController) {
    return res.status(503).json({ error: 'FormationController not initialized' });
  }
  const newZone = formationController.addNoFlyZone(req.body);
  res.json(newZone);
});

app.delete('/api/nfz/:id', (req, res) => {
  if (!formationController) {
    return res.status(503).json({ error: 'FormationController not initialized' });
  }
  const result = formationController.removeNoFlyZone(req.params.id);
  res.json({ success: result });
});

app.post('/api/formation/planPath', (req, res) => {
  if (!formationController) {
    return res.status(503).json({ error: 'FormationController not initialized' });
  }
  formationController.planPathToGoal(req.body).then(result => {
    res.json(result);
  });
});

const PORT = process.env.PORT || 3001;
server.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
  droneManager.initializeDrones(5);
});

module.exports = { io, droneManager, formationController, flightLogger };
