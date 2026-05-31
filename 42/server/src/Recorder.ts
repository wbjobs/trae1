import * as mediasoup from 'mediasoup';
import { spawn, ChildProcess } from 'child_process';
import * as path from 'path';
import * as fs from 'fs';
import { Storage } from './Storage';

type AppData = Record<string, unknown>;

export interface RecorderOptions {
	roomCode: string;
	router: mediasoup.types.Router<AppData>;
	outputDir: string;
	ffmpegPath?: string;
}

export interface RecordingResult {
	id: string;
	roomCode: string;
	fileName: string;
	filePath: string;
	fileSize: number;
	duration: number;
	startedAt: number;
	endedAt: number;
	shareUrl: string;
	expiresAt: number;
}

interface ParticipantSlot {
	peerId: string;
	producerId: string;
	plainTransport: mediasoup.types.PlainTransport<AppData>;
	rtpPort: number;
	audio?: {
		plainTransport: mediasoup.types.PlainTransport<AppData>;
		rtpPort: number;
	};
}

const BASE_RTP_PORT = 50000;
const PORTS_PER_SLOT = 4;
const MAX_SLOTS = 4;
const TILE_WIDTH = 640;
const TILE_HEIGHT = 360;
const GRID_WIDTH = TILE_WIDTH * 2;
const GRID_HEIGHT = TILE_HEIGHT * 2;
const FRAMERATE = 30;

export class Recorder {
	private id: string;
	private roomCode: string;
	private router: mediasoup.types.Router<AppData>;
	private outputDir: string;
	private ffmpegPath: string;
	private ffmpegProcess: ChildProcess | null = null;
	private slots: ParticipantSlot[] = [];
	private startedAt = 0;
	private endedAt = 0;
	private nextPort = BASE_RTP_PORT;
	private outputFilePath: string;
	private active = false;

	constructor(options: RecorderOptions) {
		this.id = this.generateRecordingId();
		this.roomCode = options.roomCode;
		this.router = options.router;
		this.outputDir = options.outputDir;
		this.ffmpegPath = options.ffmpegPath || 'ffmpeg';
		this.outputFilePath = path.join(this.outputDir, `${this.roomCode}_${this.id}.mp4`);

		if (!fs.existsSync(this.outputDir)) {
			fs.mkdirSync(this.outputDir, { recursive: true });
		}
	}

	private generateRecordingId(): string {
		return Date.now().toString(36) + Math.random().toString(36).slice(2, 8);
	}

	public isActive(): boolean {
		return this.active;
	}

	public getId(): string {
		return this.id;
	}

	public getStartedAt(): number {
		return this.startedAt;
	}

	public async start(initialProducers: { peerId: string; producerId: string; kind: string }[]): Promise<void> {
		if (this.active) return;

		this.active = true;
		this.startedAt = Date.now();
		this.nextPort = BASE_RTP_PORT;

		const videoProducers = initialProducers.filter((p) => p.kind === 'video');
		for (const producer of videoProducers.slice(0, MAX_SLOTS)) {
			await this.addParticipant(producer.peerId, producer.producerId);
		}

		this.startFFmpeg();
	}

	private async addParticipant(peerId: string, producerId: string): Promise<void> {
		if (this.slots.length >= MAX_SLOTS) return;

		const rtpPort = this.nextPort;
		this.nextPort += PORTS_PER_SLOT;

		const plainTransport = await this.router.createPlainTransport({
			listenIp: { ip: '127.0.0.1' },
			rtcpMux: false,
			comedia: false
		});

		await plainTransport.connect({
			ip: '127.0.0.1',
			port: rtpPort,
			rtcpPort: rtpPort + 1
		});

		const videoProducer = this.findProducer(producerId);
		if (videoProducer) {
			await plainTransport.produce({
				producerId: videoProducer.id
			});
		}

		const audioRtpPort = rtpPort + 2;
		let audioPlainTransport: mediasoup.types.PlainTransport<AppData> | undefined;

		const audioProducer = this.findAudioProducer(peerId);
		if (audioProducer) {
			audioPlainTransport = await this.router.createPlainTransport({
				listenIp: { ip: '127.0.0.1' },
				rtcpMux: false,
				comedia: false
			});

			await audioPlainTransport.connect({
				ip: '127.0.0.1',
				port: audioRtpPort,
				rtcpPort: audioRtpPort + 1
			});

			await audioPlainTransport.produce({
				producerId: audioProducer.id
			});
		}

		this.slots.push({
			peerId,
			producerId,
			plainTransport,
			rtpPort,
			audio: audioPlainTransport
				? { plainTransport: audioPlainTransport, rtpPort: audioRtpPort }
				: undefined
		});
	}

	private findProducer(producerId: string): mediasoup.types.Producer<AppData> | undefined {
		const internalRouter = this.router as unknown as {
			_producers: Map<string, mediasoup.types.Producer<AppData>>;
		};
		return internalRouter._producers.get(producerId);
	}

	private findAudioProducer(peerId: string): mediasoup.types.Producer<AppData> | undefined {
		const internalRouter = this.router as unknown as {
			_producers: Map<string, mediasoup.types.Producer<AppData>>;
		};
		for (const producer of internalRouter._producers.values()) {
			const appData = producer.appData as { peerId?: string; source?: string };
			if (appData.peerId === peerId && appData.source === 'mic') {
				return producer;
			}
		}
		return undefined;
	}

	private startFFmpeg(): void {
		const args: string[] = ['-hide_banner', '-loglevel', 'warning'];

		const numSlots = Math.max(1, this.slots.length);

		for (let i = 0; i < MAX_SLOTS; i++) {
			const slot = this.slots[i];
			if (slot) {
				args.push(
					'-protocol_whitelist', 'pipe,udp,rtp',
					'-f', 'sdp',
					'-i', this.generateSdp(slot.rtpPort, 'video')
				);
			} else {
				args.push(
					'-f', 'lavfi',
					'-i', `color=c=0x1a1a2e:size=${TILE_WIDTH}x${TILE_HEIGHT}:rate=${FRAMERATE}`
				);
			}
		}

		for (let i = 0; i < MAX_SLOTS; i++) {
			const slot = this.slots[i];
			if (slot?.audio) {
				args.push(
					'-protocol_whitelist', 'pipe,udp,rtp',
					'-f', 'sdp',
					'-i', this.generateSdp(slot.audio.rtpPort, 'audio')
				);
			}
		}

		const filterParts: string[] = [];

		for (let i = 0; i < MAX_SLOTS; i++) {
			const row = Math.floor(i / 2);
			const col = i % 2;
			const x = col * TILE_WIDTH;
			const y = row * TILE_HEIGHT;
			filterParts.push(`[${i}:v]scale=${TILE_WIDTH}:${TILE_HEIGHT},setpts=PTS-STARTPTS,drawtext=text='':x=10:y=10:fontsize=20:fontcolor=white[v${i}]`);
		}

		const overlayChain: string[] = [];
		if (MAX_SLOTS >= 1) {
			overlayChain.push(`[base][v0]overlay=0:0[tmp0]`);
		}
		if (MAX_SLOTS >= 2) {
			overlayChain.push(`[tmp0][v1]overlay=${TILE_WIDTH}:0[tmp1]`);
		}
		if (MAX_SLOTS >= 3) {
			overlayChain.push(`[tmp1][v2]overlay=0:${TILE_HEIGHT}[tmp2]`);
		}
		if (MAX_SLOTS >= 4) {
			overlayChain.push(`[tmp2][v3]overlay=${TILE_WIDTH}:${TILE_HEIGHT}[outv]`);
		}

		const numAudios = this.slots.filter((s) => s.audio).length;

		let filterComplex = '';
		if (numSlots === 1) {
			filterComplex = `[0:v]scale=${GRID_WIDTH}:${GRID_HEIGHT},setpts=PTS-STARTPTS[outv]`;
		} else {
			filterComplex = `color=c=0x0f0f14:s=${GRID_WIDTH}x${GRID_HEIGHT}[base];`;
			for (let i = 0; i < MAX_SLOTS; i++) {
				const row = Math.floor(i / 2);
				const col = i % 2;
				const x = col * TILE_WIDTH;
				const y = row * TILE_HEIGHT;
				filterComplex += `[${i}:v]scale=${TILE_WIDTH}:${TILE_HEIGHT},setpts=PTS-STARTPTS[v${i}];`;
			}
			for (let i = 0; i < MAX_SLOTS; i++) {
				const row = Math.floor(i / 2);
				const col = i % 2;
				const x = col * TILE_WIDTH;
				const y = row * TILE_HEIGHT;
				if (i === 0) {
					filterComplex += `[base][v0]overlay=${x}:${y}[tmp0];`;
				} else if (i < MAX_SLOTS - 1) {
					filterComplex += `[tmp${i - 1}][v${i}]overlay=${x}:${y}[tmp${i}];`;
				} else {
					filterComplex += `[tmp${i - 1}][v${i}]overlay=${x}:${y}[outv];`;
				}
			}
		}

		if (numAudios > 0) {
			const audioInputs: string[] = [];
			let audioInputIdx = MAX_SLOTS;
			for (let i = 0; i < MAX_SLOTS; i++) {
				if (this.slots[i]?.audio) {
					audioInputs.push(`[${audioInputIdx}:a]`);
					audioInputIdx++;
				}
			}
			if (audioInputs.length > 1) {
				filterComplex += `${audioInputs.join('')}amix=inputs=${audioInputs.length}:duration=longest[outa]`;
			} else {
				filterComplex += `${audioInputs[0]}aresample=48000[outa]`;
			}
		}

		if (filterComplex) {
			args.push('-filter_complex', filterComplex);
		}

		args.push('-map', '[outv]');
		if (numAudios > 0) {
			args.push('-map', '[outa]');
			args.push('-c:a', 'aac', '-b:a', '128k');
		}

		args.push(
			'-c:v', 'libx264',
			'-preset', 'veryfast',
			'-crf', '23',
			'-pix_fmt', 'yuv420p',
			'-movflags', '+faststart',
			'-y',
			this.outputFilePath
		);

		console.log('[Recorder] Starting FFmpeg with args:', args.join(' '));

		this.ffmpegProcess = spawn(this.ffmpegPath, args, {
			stdio: ['ignore', 'pipe', 'pipe']
		});

		this.ffmpegProcess.stdout?.on('data', (data: Buffer) => {
			console.log('[FFmpeg]', data.toString().trim());
		});

		this.ffmpegProcess.stderr?.on('data', (data: Buffer) => {
			console.warn('[FFmpeg stderr]', data.toString().trim());
		});

		this.ffmpegProcess.on('exit', (code) => {
			console.log(`[Recorder] FFmpeg exited with code ${code}`);
			this.active = false;
			this.endedAt = Date.now();
			this.cleanupTransports();
		});

		this.ffmpegProcess.on('error', (err) => {
			console.error('[Recorder] FFmpeg error:', err);
			this.active = false;
			this.endedAt = Date.now();
			this.cleanupTransports();
		});
	}

	private generateSdp(port: number, kind: 'video' | 'audio'): string {
		const payloadType = kind === 'video' ? 96 : 100;
		const codec = kind === 'video' ? 'VP8' : 'opus';
		const clockRate = kind === 'video' ? 90000 : 48000;
		const channels = kind === 'audio' ? '/2' : '';

		return (
			`v=0\r\n` +
			`o=- 0 0 IN IP4 127.0.0.1\r\n` +
			`s=- \r\n` +
			`c=IN IP4 127.0.0.1\r\n` +
			`t=0 0\r\n` +
			`m=${kind} ${port} RTP/AVP ${payloadType}\r\n` +
			`a=rtpmap:${payloadType} ${codec}/${clockRate}${channels}\r\n` +
			`a=sendrecv\r\n`
		);
	}

	private cleanupTransports(): void {
		for (const slot of this.slots) {
			try {
				slot.plainTransport.close();
			} catch (_) { /* ignore */ }
			if (slot.audio) {
				try {
					slot.audio.plainTransport.close();
				} catch (_) { /* ignore */ }
			}
		}
		this.slots = [];
	}

	public async stop(storage?: Storage): Promise<RecordingResult | null> {
		if (!this.active && !this.ffmpegProcess) return null;

		return new Promise((resolve) => {
			const finish = () => {
				this.active = false;
				this.endedAt = Date.now();
				this.cleanupTransports();

				const fileExists = fs.existsSync(this.outputFilePath);
				const fileSize = fileExists ? fs.statSync(this.outputFilePath).size : 0;
				const duration = (this.endedAt - this.startedAt) / 1000;

				const result: RecordingResult = {
					id: this.id,
					roomCode: this.roomCode,
					fileName: path.basename(this.outputFilePath),
					filePath: this.outputFilePath,
					fileSize,
					duration,
					startedAt: this.startedAt,
					endedAt: this.endedAt,
					shareUrl: '',
					expiresAt: this.endedAt + 7 * 24 * 60 * 60 * 1000
				};

				if (storage && fileExists && fileSize > 0) {
					storage.upload(result).then((url) => {
						result.shareUrl = url;
						resolve(result);
					}).catch((err) => {
						console.error('[Recorder] Upload failed:', err);
						resolve(result);
					});
				} else {
					resolve(result);
				}
			};

			if (this.ffmpegProcess && this.ffmpegProcess.exitCode === null) {
				this.ffmpegProcess.once('exit', () => finish());
				this.ffmpegProcess.kill('SIGINT');

				setTimeout(() => {
					if (this.ffmpegProcess && this.ffmpegProcess.exitCode === null) {
						this.ffmpegProcess.kill('SIGKILL');
					}
				}, 5000);
			} else {
				finish();
			}
		});
	}

	public forceCleanup(): void {
		if (this.ffmpegProcess && this.ffmpegProcess.exitCode === null) {
			this.ffmpegProcess.kill('SIGKILL');
		}
		this.cleanupTransports();
		if (fs.existsSync(this.outputFilePath)) {
			try {
				fs.unlinkSync(this.outputFilePath);
			} catch (_) { /* ignore */ }
		}
	}
}
