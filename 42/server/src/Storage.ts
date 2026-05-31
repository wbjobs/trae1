import * as fs from 'fs';
import * as path from 'path';
import type { RecordingResult } from './Recorder';

export interface StorageConfig {
	type: 'local' | 'oss';
	local?: {
		baseUrl: string;
		recordingDir: string;
	};
	oss?: {
		accessKeyId: string;
		accessKeySecret: string;
		bucket: string;
		region: string;
		endpoint?: string;
		baseUrl: string;
	};
}

interface RecordingMetadata {
	id: string;
	roomCode: string;
	fileName: string;
	fileSize: number;
	duration: number;
	startedAt: number;
	endedAt: number;
	expiresAt: number;
	url: string;
}

const STORAGE_DIR = path.join(process.cwd(), 'recordings');
const METADATA_FILE = path.join(STORAGE_DIR, 'metadata.json');

export class Storage {
	private config: StorageConfig;
	private metadata: Map<string, RecordingMetadata> = new Map();
	private cleanupTimer: NodeJS.Timeout | null = null;

	constructor(config: StorageConfig) {
		this.config = config;
		this.ensureDirs();
		this.loadMetadata();
		this.startCleanupJob();
	}

	private ensureDirs(): void {
		if (!fs.existsSync(STORAGE_DIR)) {
			fs.mkdirSync(STORAGE_DIR, { recursive: true });
		}
	}

	private loadMetadata(): void {
		if (fs.existsSync(METADATA_FILE)) {
			try {
				const raw = fs.readFileSync(METADATA_FILE, 'utf-8');
				const arr: RecordingMetadata[] = JSON.parse(raw);
				for (const item of arr) {
					this.metadata.set(item.id, item);
				}
			} catch (err) {
				console.error('[Storage] Failed to load metadata:', err);
			}
		}
	}

	private saveMetadata(): void {
		const arr = Array.from(this.metadata.values());
		fs.writeFileSync(METADATA_FILE, JSON.stringify(arr, null, 2), 'utf-8');
	}

	private startCleanupJob(): void {
		const CHECK_INTERVAL = 60 * 60 * 1000;
		this.cleanupTimer = setInterval(() => this.cleanupExpired(), CHECK_INTERVAL);
	}

	public cleanupExpired(): void {
		const now = Date.now();
		const toDelete: string[] = [];

		for (const [id, meta] of this.metadata.entries()) {
			if (now >= meta.expiresAt) {
				toDelete.push(id);
			}
		}

		for (const id of toDelete) {
			this.deleteRecording(id);
		}

		if (toDelete.length > 0) {
			console.log(`[Storage] Cleaned up ${toDelete.length} expired recordings`);
		}
	}

	public deleteRecording(id: string): void {
		const meta = this.metadata.get(id);
		if (!meta) return;

		const filePath = path.join(STORAGE_DIR, meta.fileName);
		if (fs.existsSync(filePath)) {
			try {
				fs.unlinkSync(filePath);
			} catch (_) { /* ignore */ }
		}

		this.metadata.delete(id);
		this.saveMetadata();
		console.log(`[Storage] Deleted recording ${id}`);
	}

	public async upload(recording: RecordingResult): Promise<string> {
		if (this.config.type === 'local' || !this.config.oss) {
			return this.uploadLocal(recording);
		}

		return this.uploadOss(recording);
	}

	private async uploadLocal(recording: RecordingResult): Promise<string> {
		const baseUrl = (this.config.local?.baseUrl || 'http://localhost:3001').replace(/\/$/, '');
		const url = `${baseUrl}/recordings/${recording.fileName}`;

		const destPath = path.join(STORAGE_DIR, recording.fileName);
		if (recording.filePath !== destPath && fs.existsSync(recording.filePath)) {
			fs.copyFileSync(recording.filePath, destPath);
		}

		const meta: RecordingMetadata = {
			id: recording.id,
			roomCode: recording.roomCode,
			fileName: recording.fileName,
			fileSize: recording.fileSize,
			duration: recording.duration,
			startedAt: recording.startedAt,
			endedAt: recording.endedAt,
			expiresAt: recording.expiresAt,
			url
		};

		this.metadata.set(recording.id, meta);
		this.saveMetadata();

		return url;
	}

	private async uploadOss(recording: RecordingResult): Promise<string> {
		const ossConfig = this.config.oss!;
		const baseUrl = ossConfig.baseUrl.replace(/\/$/, '');
		const objectKey = `recordings/${recording.fileName}`;
		const url = `${baseUrl}/${objectKey}`;

		try {
			const OSS = await import('ali-oss');
			const client = new OSS({
				accessKeyId: ossConfig.accessKeyId,
				accessKeySecret: ossConfig.accessKeySecret,
				bucket: ossConfig.bucket,
				region: ossConfig.region,
				endpoint: ossConfig.endpoint
			});

			const fileBuffer = fs.readFileSync(recording.filePath);
			await client.put(objectKey, fileBuffer);

			const meta: RecordingMetadata = {
				id: recording.id,
				roomCode: recording.roomCode,
				fileName: recording.fileName,
				fileSize: recording.fileSize,
				duration: recording.duration,
				startedAt: recording.startedAt,
				endedAt: recording.endedAt,
				expiresAt: recording.expiresAt,
				url
			};

			this.metadata.set(recording.id, meta);
			this.saveMetadata();

			if (fs.existsSync(recording.filePath)) {
				try {
					fs.unlinkSync(recording.filePath);
				} catch (_) { /* ignore */ }
			}

			console.log(`[Storage] Uploaded recording to OSS: ${url}`);
			return url;
		} catch (err) {
			console.error('[Storage] OSS upload failed, falling back to local:', err);
			return this.uploadLocal(recording);
		}
	}

	public getRecording(id: string): RecordingMetadata | undefined {
		return this.metadata.get(id);
	}

	public getRecordingsByRoom(roomCode: string): RecordingMetadata[] {
		const result: RecordingMetadata[] = [];
		for (const meta of this.metadata.values()) {
			if (meta.roomCode === roomCode) {
				result.push(meta);
			}
		}
		return result.sort((a, b) => b.startedAt - a.startedAt);
	}

	public getRecordingFilePath(id: string): string | null {
		const meta = this.metadata.get(id);
		if (!meta) return null;
		const filePath = path.join(STORAGE_DIR, meta.fileName);
		return fs.existsSync(filePath) ? filePath : null;
	}

	public shutdown(): void {
		if (this.cleanupTimer) {
			clearInterval(this.cleanupTimer);
		}
		this.cleanupExpired();
	}
}

export function createStorageFromEnv(): Storage {
	const type = (process.env.STORAGE_TYPE || 'local') as 'local' | 'oss';

	const config: StorageConfig = {
		type,
		local: {
			baseUrl: process.env.LOCAL_STORAGE_URL || 'http://localhost:3001',
			recordingDir: STORAGE_DIR
		}
	};

	if (type === 'oss') {
		config.oss = {
			accessKeyId: process.env.OSS_ACCESS_KEY_ID || '',
			accessKeySecret: process.env.OSS_ACCESS_KEY_SECRET || '',
			bucket: process.env.OSS_BUCKET || '',
			region: process.env.OSS_REGION || '',
			endpoint: process.env.OSS_ENDPOINT,
			baseUrl: process.env.OSS_BASE_URL || ''
		};
	}

	return new Storage(config);
}
