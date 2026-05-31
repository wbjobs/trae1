import { io, Socket } from 'socket.io-client';
import type { RoomState, PeerState, ProducerInfo } from './types';

const SERVER_URL = import.meta.env.VITE_SERVER_URL || 'http://localhost:3001';

type Callback<T = unknown> = (response: { ok: boolean; error?: string; [key: string]: unknown } & T) => void;

export class SignalingClient {
	private socket: Socket;
	private listeners: Map<string, Set<(data: unknown) => void>> = new Map();

	constructor() {
		this.socket = io(SERVER_URL, {
			transports: ['websocket'],
			autoConnect: false
		});

		this.socket.on('room:state', (state: RoomState) => this.emit('room:state', state));
		this.socket.on('peer:joined', (peer: PeerState) => this.emit('peer:joined', peer));
		this.socket.on('peer:left', (data: { peerId: string }) => this.emit('peer:left', data));
		this.socket.on('peer:state-changed', (peer: PeerState) => this.emit('peer:state-changed', peer));
		this.socket.on('producer:added', (info: ProducerInfo) => this.emit('producer:added', info));
		this.socket.on('producer:removed', (info: { peerId: string; producerId: string }) =>
			this.emit('producer:removed', info)
		);
		this.socket.on('error', (err: { message: string }) => this.emit('error', err));
		this.socket.on('disconnect', () => this.emit('disconnect', null));
		this.socket.on('connect', () => this.emit('connect', null));
	}

	public connect(): Promise<void> {
		return new Promise((resolve, reject) => {
			this.socket.once('connect_error', reject);
			this.socket.once('connect', () => {
				this.socket.off('connect_error', reject);
				resolve();
			});
			this.socket.connect();
		});
	}

	public disconnect(): void {
		this.socket.disconnect();
	}

	public get id(): string {
		return this.socket.id;
	}

	public get connected(): boolean {
		return this.socket.connected;
	}

	public on(event: string, callback: (data: unknown) => void): () => void {
		if (!this.listeners.has(event)) {
			this.listeners.set(event, new Set());
		}
		this.listeners.get(event)!.add(callback);
		return () => {
			this.listeners.get(event)?.delete(callback);
		};
	}

	private emit(event: string, data: unknown): void {
		this.listeners.get(event)?.forEach((cb) => cb(data));
	}

	public createRoom(name: string): Promise<{ code: string; peerId: string; routerRtpCapabilities: mediasoupClient.types.RtpCapabilities }> {
		return new Promise((resolve, reject) => {
			this.socket.emit(
				'room:create',
				{ name },
				(response: { ok: boolean; error?: string; code?: string; peerId?: string; routerRtpCapabilities?: mediasoupClient.types.RtpCapabilities }) => {
					if (response.ok) {
						this.socket.once(
							'room:created',
							(info: { code: string; peerId: string; routerRtpCapabilities: mediasoupClient.types.RtpCapabilities }) => {
								resolve(info);
							}
						);
					} else {
						reject(new Error(response.error || 'Failed to create room'));
					}
				}
			);
		});
	}

	public joinRoom(code: string, name: string): Promise<{ code: string; peerId: string; routerRtpCapabilities: mediasoupClient.types.RtpCapabilities }> {
		return new Promise((resolve, reject) => {
			this.socket.emit(
				'room:join',
				{ code, name },
				(response: { ok: boolean; error?: string }) => {
					if (response.ok) {
						this.socket.once(
							'room:joined',
							(info: { code: string; peerId: string; routerRtpCapabilities: mediasoupClient.types.RtpCapabilities }) => {
								resolve(info);
							}
						);
					} else {
						reject(new Error(response.error || 'Failed to join room'));
					}
				}
			);
		});
	}

	public leaveRoom(): void {
		this.socket.emit('room:leave');
	}

	public createTransport(direction: 'send' | 'recv'): Promise<{
		id: string;
		iceParameters: mediasoupClient.types.IceParameters;
		iceCandidates: mediasoupClient.types.IceCandidate[];
		dtlsParameters: mediasoupClient.types.DtlsParameters;
	}> {
		return new Promise((resolve, reject) => {
			this.socket.emit(
				'transport:create',
				{ direction },
				(response: { ok: boolean; error?: string; id?: string; iceParameters?: mediasoupClient.types.IceParameters; iceCandidates?: mediasoupClient.types.IceCandidate[]; dtlsParameters?: mediasoupClient.types.DtlsParameters }) => {
					if (response.ok && response.id) {
						resolve({
							id: response.id,
							iceParameters: response.iceParameters!,
							iceCandidates: response.iceCandidates!,
							dtlsParameters: response.dtlsParameters!
						});
					} else {
						reject(new Error(response.error || 'Failed to create transport'));
					}
				}
			);
		});
	}

	public connectTransport(transportId: string, dtlsParameters: mediasoupClient.types.DtlsParameters): Promise<void> {
		return new Promise((resolve, reject) => {
			this.socket.emit(
				'transport:connect',
				{ transportId, dtlsParameters },
				(response: { ok: boolean; error?: string }) => {
					if (response.ok) {
						resolve();
					} else {
						reject(new Error(response.error || 'Failed to connect transport'));
					}
				}
			);
		});
	}

	public restartIce(transportId: string): Promise<mediasoupClient.types.IceParameters> {
		return new Promise((resolve, reject) => {
			this.socket.emit(
				'transport:restart-ice',
				{ transportId },
				(response: { ok: boolean; error?: string; iceParameters?: mediasoupClient.types.IceParameters }) => {
					if (response.ok && response.iceParameters) {
						resolve(response.iceParameters);
					} else {
						reject(new Error(response.error || 'Failed to restart ICE'));
					}
				}
			);
		});
	}

	public produce(params: {
		transportId: string;
		kind: mediasoupClient.types.MediaKind;
		rtpParameters: mediasoupClient.types.RtpParameters;
		appData?: Record<string, unknown>;
	}): Promise<{ id: string }> {
		return new Promise((resolve, reject) => {
			this.socket.emit(
				'produce',
				params,
				(response: { ok: boolean; error?: string; id?: string }) => {
					if (response.ok && response.id) {
						resolve({ id: response.id });
					} else {
						reject(new Error(response.error || 'Failed to produce'));
					}
				}
			);
		});
	}

	public consume(params: {
		producerPeerId: string;
		producerId: string;
		rtpCapabilities: mediasoupClient.types.RtpCapabilities;
	}): Promise<{
		id: string;
		kind: mediasoupClient.types.MediaKind;
		rtpParameters: mediasoupClient.types.RtpParameters;
		producerPaused: boolean;
	}> {
		return new Promise((resolve, reject) => {
			this.socket.emit(
				'consume',
				params,
				(response: { ok: boolean; error?: string; id?: string; kind?: mediasoupClient.types.MediaKind; rtpParameters?: mediasoupClient.types.RtpParameters; producerPaused?: boolean }) => {
					if (response.ok && response.id) {
						resolve({
							id: response.id,
							kind: response.kind!,
							rtpParameters: response.rtpParameters!,
							producerPaused: response.producerPaused!
						});
					} else {
						reject(new Error(response.error || 'Failed to consume'));
					}
				}
			);
		});
	}

	public updateMediaState(state: { cameraOn: boolean; micOn: boolean; screenSharing: boolean }): void {
		this.socket.emit('peer:media-state', state);
	}

	public startRecording(): Promise<{ ok: boolean; error?: string; recordingId?: string }> {
		return new Promise((resolve) => {
			this.socket.emit('recording:start', {}, (response: { ok: boolean; error?: string; recordingId?: string }) => {
				resolve(response);
			});
		});
	}

	public stopRecording(): Promise<{ ok: boolean; error?: string; result?: { id: string; shareUrl: string; duration: number; fileSize: number } }> {
		return new Promise((resolve) => {
			this.socket.emit('recording:stop', {}, (response: { ok: boolean; error?: string; result?: { id: string; shareUrl: string; duration: number; fileSize: number } }) => {
				resolve(response);
			});
		});
	}
}

export const signaling = new SignalingClient();
