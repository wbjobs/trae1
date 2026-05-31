import express from 'express';
import http from 'http';
import cors from 'cors';
import path from 'path';
import fs from 'fs';
import { Server as SocketIOServer, Socket } from 'socket.io';
import * as mediasoup from 'mediasoup';
import { Room } from './Room';
import { Peer } from './Peer';
import { Recorder, RecordingResult } from './Recorder';
import { createStorageFromEnv, Storage } from './Storage';
import {
	LISTEN_IP,
	LISTEN_PORT,
	WORKER_NUM,
	mediasoupWorkerRtcMinPort,
	mediasoupWorkerRtcMaxPort,
	mediasoupWebRtcServerOptions,
	routerOptions
} from './config';
import { PeerState } from './types';

const app = express();
app.use(cors());

const server = http.createServer(app);

const io = new SocketIOServer(server, {
	cors: { origin: '*' }
});

const workers: mediasoup.types.Worker[] = [];
const webRtcServers: mediasoup.types.WebRtcServer[] = [];
const rooms = new Map<string, Room>();
const socketToRoom = new Map<string, string>();
const socketToPeerId = new Map<string, string>();

const roomRecorders = new Map<string, Recorder>();
const recordingResults = new Map<string, RecordingResult>();
const storage = createStorageFromEnv();

const RECORDINGS_DIR = path.join(process.cwd(), 'recordings');
if (!fs.existsSync(RECORDINGS_DIR)) {
	fs.mkdirSync(RECORDINGS_DIR, { recursive: true });
}

app.use('/recordings', express.static(RECORDINGS_DIR));

app.get('/api/recordings/:id', (req, res) => {
	const meta = storage.getRecording(req.params.id);
	if (!meta) {
		res.status(404).json({ error: 'Recording not found' });
		return;
	}
	const filePath = storage.getRecordingFilePath(req.params.id);
	if (filePath) {
		const stat = fs.statSync(filePath);
		const fileSize = stat.size;
		const range = req.headers.range;

		if (range) {
			const parts = range.replace(/bytes=/, '').split('-');
			const start = parseInt(parts[0], 10);
			const end = parts[1] ? parseInt(parts[1], 10) : fileSize - 1;
			const chunkSize = end - start + 1;
			const file = fs.createReadStream(filePath, { start, end });
			res.writeHead(206, {
				'Content-Range': `bytes ${start}-${end}/${fileSize}`,
				'Accept-Ranges': 'bytes',
				'Content-Length': chunkSize,
				'Content-Type': 'video/mp4'
			});
			file.pipe(res);
		} else {
			res.writeHead(200, {
				'Content-Length': fileSize,
				'Content-Type': 'video/mp4',
				'Accept-Ranges': 'bytes'
			});
			fs.createReadStream(filePath).pipe(res);
		}
	} else {
		res.json({ id: meta.id, url: meta.url, duration: meta.duration, expiresAt: meta.expiresAt });
	}
});

app.get('/api/recordings/room/:code', (req, res) => {
	const recordings = storage.getRecordingsByRoom(req.params.code);
	res.json(recordings);
});

async function initWorkers(): Promise<void> {
	for (let i = 0; i < WORKER_NUM; i++) {
		const worker = await mediasoup.createWorker({
			rtcMinPort: mediasoupWorkerRtcMinPort + i * 100,
			rtcMaxPort: mediasoupWorkerRtcMaxPort + i * 100
		});

		worker.on('died', () => {
			console.error('mediasoup worker died, exiting');
			process.exit(1);
		});

		const webRtcServer = await worker.createWebRtcServer(mediasoupWebRtcServerOptions as any);
		workers.push(worker);
		webRtcServers.push(webRtcServer);
	}
}

function pickWorkerIndex(): number {
	return Math.floor(Math.random() * workers.length);
}

function broadcastRoomState(room: Room): void {
	const peers: PeerState[] = [];
	for (const peer of room.peers.values()) {
		peers.push(peer.toState());
	}

	const producers: { peerId: string; producerId: string; kind: string; paused: boolean }[] = [];
	for (const peer of room.peers.values()) {
		for (const producer of peer.producers.values()) {
			producers.push({
				peerId: peer.id,
				producerId: producer.id,
				kind: producer.kind,
				paused: producer.paused
			});
		}
	}

	const isRecording = roomRecorders.has(room.code);
	const recorder = roomRecorders.get(room.code);

	io.to(room.code).emit('room:state', {
		code: room.code,
		peers,
		producers,
		recording: isRecording ? {
			active: true,
			startedAt: recorder!.getStartedAt(),
			recordingId: recorder!.getId()
		} : null
	});
}

async function createRoom(socket: Socket, name: string): Promise<void> {
	const workerIndex = pickWorkerIndex();
	const worker = workers[workerIndex];
	const webRtcServer = webRtcServers[workerIndex];
	const router = await worker.createRouter(routerOptions as any);

	const room = new Room({
		router,
		webRtcServer,
		events: {
			onStateChange: broadcastRoomState,
			onDestroyed: (r) => {
				rooms.delete(r.code);
				const rec = roomRecorders.get(r.code);
				if (rec) {
					rec.forceCleanup();
					roomRecorders.delete(r.code);
				}
			},
			onPeerJoined: (r, peer) => {
				io.to(r.code).emit('peer:joined', peer.toState());
			},
			onPeerLeft: (r, peer) => {
				io.to(r.code).emit('peer:left', { peerId: peer.id });
				if (r.peers.size === 0) {
					r.close();
				}
			},
			onProducerAdded: (r, peer, producer) => {
				io.to(r.code).emit('producer:added', {
					peerId: peer.id,
					producerId: producer.id,
					kind: producer.kind,
					paused: producer.paused
				});
			},
			onProducerRemoved: (r, peer, producerId) => {
				io.to(r.code).emit('producer:removed', {
					peerId: peer.id,
					producerId
				});
			},
			onPeerStateChanged: (r, peer) => {
				io.to(r.code).emit('peer:state-changed', peer.toState());
			}
		}
	});

	rooms.set(room.code, room);

	const peerId = socket.id;
	const peer = new Peer(peerId, name);
	room.addPeer(peer);

	socketToRoom.set(socket.id, room.code);
	socketToPeerId.set(socket.id, peerId);
	socket.join(room.code);

	socket.emit('room:created', {
		code: room.code,
		peerId,
		routerRtpCapabilities: room.routerRtpCapabilities
	});
}

async function joinRoom(socket: Socket, code: string, name: string): Promise<void> {
	const room = rooms.get(code);
	if (!room) {
		socket.emit('error', { message: 'Room not found' });
		return;
	}

	if (room.hasReachedCapacity()) {
		socket.emit('error', { message: 'Room is full' });
		return;
	}

	const peerId = socket.id;
	const peer = new Peer(peerId, name);
	room.addPeer(peer);

	socketToRoom.set(socket.id, room.code);
	socketToPeerId.set(socket.id, peerId);
	socket.join(room.code);

	socket.emit('room:joined', {
		code: room.code,
		peerId,
		routerRtpCapabilities: room.routerRtpCapabilities
	});
}

function handleDisconnect(socket: Socket): void {
	const roomCode = socketToRoom.get(socket.id);
	const peerId = socketToPeerId.get(socket.id);
	if (!roomCode || !peerId) return;

	const room = rooms.get(roomCode);
	if (room) {
		room.removePeer(peerId);
	}

	socketToRoom.delete(socket.id);
	socketToPeerId.delete(socket.id);
}

async function startRecording(roomCode: string, peerId: string): Promise<{ ok: boolean; error?: string; recordingId?: string }> {
	const room = rooms.get(roomCode);
	if (!room) return { ok: false, error: 'Room not found' };

	if (roomRecorders.has(roomCode)) {
		return { ok: false, error: 'Recording already in progress' };
	}

	const peer = room.getPeer(peerId);
	if (!peer) return { ok: false, error: 'Peer not found' };

	const producers: { peerId: string; producerId: string; kind: string }[] = [];
	for (const p of room.peers.values()) {
		for (const producer of p.producers.values()) {
			if (producer.kind === 'video') {
				producers.push({
					peerId: p.id,
					producerId: producer.id,
					kind: producer.kind
				});
			}
		}
	}

	const recorder = new Recorder({
		roomCode,
		router: room.router,
		outputDir: RECORDINGS_DIR
	});

	try {
		await recorder.start(producers);
		roomRecorders.set(roomCode, recorder);

		io.to(roomCode).emit('recording:started', {
			recordingId: recorder.getId(),
			startedAt: recorder.getStartedAt()
		});

		broadcastRoomState(room);

		return { ok: true, recordingId: recorder.getId() };
	} catch (err: any) {
		recorder.forceCleanup();
		return { ok: false, error: err.message };
	}
}

async function stopRecording(roomCode: string): Promise<{ ok: boolean; error?: string; result?: RecordingResult }> {
	const recorder = roomRecorders.get(roomCode);
	if (!recorder) {
		return { ok: false, error: 'No recording in progress' };
	}

	roomRecorders.delete(roomCode);

	const result = await recorder.stop(storage);

	if (result) {
		recordingResults.set(result.id, result);

		io.to(roomCode).emit('recording:stopped', {
			recordingId: result.id,
			shareUrl: result.shareUrl,
			duration: result.duration,
			fileSize: result.fileSize
		});

		const room = rooms.get(roomCode);
		if (room) {
			broadcastRoomState(room);
		}
	}

	return { ok: true, result: result || undefined };
}

io.on('connection', (socket) => {
	console.log('socket connected:', socket.id);

	socket.on('room:create', async ({ name }, callback) => {
		try {
			await createRoom(socket, name);
			callback?.({ ok: true });
		} catch (err: any) {
			callback?.({ ok: false, error: err.message });
		}
	});

	socket.on('room:join', async ({ code, name }, callback) => {
		try {
			await joinRoom(socket, code, name);
			callback?.({ ok: true });
		} catch (err: any) {
			callback?.({ ok: false, error: err.message });
		}
	});

	socket.on('room:leave', () => {
		handleDisconnect(socket);
	});

	socket.on('transport:create', async ({ direction }, callback) => {
		try {
			const roomCode = socketToRoom.get(socket.id);
			const peerId = socketToPeerId.get(socket.id);
			if (!roomCode || !peerId) throw new Error('Not in a room');

			const room = rooms.get(roomCode)!;
			const transportInfo = await room.createWebRtcTransport(peerId, direction);
			callback?.({ ok: true, ...transportInfo });
		} catch (err: any) {
			callback?.({ ok: false, error: err.message });
		}
	});

	socket.on('transport:connect', async ({ transportId, dtlsParameters }, callback) => {
		try {
			const roomCode = socketToRoom.get(socket.id);
			const peerId = socketToPeerId.get(socket.id);
			if (!roomCode || !peerId) throw new Error('Not in a room');

			const room = rooms.get(roomCode)!;
			await room.connectTransport(transportId, dtlsParameters, peerId);
			callback?.({ ok: true });
		} catch (err: any) {
			callback?.({ ok: false, error: err.message });
		}
	});

	socket.on('transport:restart-ice', async ({ transportId }, callback) => {
		try {
			const roomCode = socketToRoom.get(socket.id);
			const peerId = socketToPeerId.get(socket.id);
			if (!roomCode || !peerId) throw new Error('Not in a room');

			const room = rooms.get(roomCode)!;
			const iceParameters = await room.restartTransportIce(transportId, peerId);
			callback?.({ ok: true, iceParameters });
		} catch (err: any) {
			callback?.({ ok: false, error: err.message });
		}
	});

	socket.on('produce', async ({ transportId, kind, rtpParameters, appData }, callback) => {
		try {
			const roomCode = socketToRoom.get(socket.id);
			const peerId = socketToPeerId.get(socket.id);
			if (!roomCode || !peerId) throw new Error('Not in a room');

			const room = rooms.get(roomCode)!;
			const result = await room.produce({ peerId, transportId, kind, rtpParameters, appData });
			callback?.({ ok: true, id: result.id });
		} catch (err: any) {
			callback?.({ ok: false, error: err.message });
		}
	});

	socket.on('consume', async ({ producerPeerId, producerId, rtpCapabilities }, callback) => {
		try {
			const roomCode = socketToRoom.get(socket.id);
			const peerId = socketToPeerId.get(socket.id);
			if (!roomCode || !peerId) throw new Error('Not in a room');

			const room = rooms.get(roomCode)!;
			const result = await room.consume({
				consumerPeerId: peerId,
				producerPeerId,
				producerId,
				rtpCapabilities
			});
			callback?.({ ok: true, ...result });
		} catch (err: any) {
			callback?.({ ok: false, error: err.message });
		}
	});

	socket.on('peer:media-state', ({ cameraOn, micOn, screenSharing }) => {
		const roomCode = socketToRoom.get(socket.id);
		const peerId = socketToPeerId.get(socket.id);
		if (!roomCode || !peerId) return;

		const room = rooms.get(roomCode);
		if (!room) return;

		room.setPeerMediaState(peerId, { cameraOn, micOn, screenSharing });
	});

	socket.on('recording:start', async (_data, callback) => {
		const roomCode = socketToRoom.get(socket.id);
		const peerId = socketToPeerId.get(socket.id);
		if (!roomCode || !peerId) {
			callback?.({ ok: false, error: 'Not in a room' });
			return;
		}

		const result = await startRecording(roomCode, peerId);
		callback?.(result);
	});

	socket.on('recording:stop', async (_data, callback) => {
		const roomCode = socketToRoom.get(socket.id);
		if (!roomCode) {
			callback?.({ ok: false, error: 'Not in a room' });
			return;
		}

		const result = await stopRecording(roomCode);
		callback?.(result);
	});

	socket.on('disconnect', () => {
		console.log('socket disconnected:', socket.id);
		handleDisconnect(socket);
	});
});

async function main(): Promise<void> {
	await initWorkers();
	server.listen(LISTEN_PORT, LISTEN_IP, () => {
		console.log(`Server listening on ${LISTEN_IP}:${LISTEN_PORT}`);
		console.log(`Recordings served from: ${RECORDINGS_DIR}`);
	});
}

main().catch((err) => {
	console.error('Failed to start server:', err);
	process.exit(1);
});

process.on('SIGTERM', () => {
	console.log('Shutting down...');
	storage.shutdown();
	for (const recorder of roomRecorders.values()) {
		recorder.forceCleanup();
	}
	process.exit(0);
});
