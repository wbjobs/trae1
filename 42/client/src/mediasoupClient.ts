import * as mediasoupClient from 'mediasoup-client';
import { signaling } from './signaling';
import type { ProducerInfo } from './types';

export type MediaSource = 'camera' | 'screen';

export interface LocalStream {
	track: MediaStreamTrack;
	stream: MediaStream;
	source: MediaSource;
}

export class MediasoupClient {
	private device: mediasoupClient.Device | null = null;
	private sendTransport: mediasoupClient.types.Transport | null = null;
	private recvTransport: mediasoupClient.types.Transport | null = null;
	private cameraProducer: mediasoupClient.types.Producer | null = null;
	private micProducer: mediasoupClient.types.Producer | null = null;
	private screenProducer: mediasoupClient.types.Producer | null = null;
	private consumers = new Map<string, mediasoupClient.types.Consumer>();
	private remoteStreams = new Map<string, MediaStream>();

	private onRemoteStreamAdded: ((peerId: string, stream: MediaStream, kind: string) => void) | null = null;
	private onRemoteStreamRemoved: ((peerId: string, kind: string) => void) | null = null;
	private onIceRestarting: ((restarting: boolean) => void) | null = null;
	private iceRestartInProgress = false;
	private consecutiveRestartFailures = 0;
	private readonly MAX_RESTART_FAILURES = 3;
	private readonly ICE_RESTART_TIMEOUT_MS = 2500;

	public async load(routerRtpCapabilities: mediasoupClient.types.RtpCapabilities): Promise<void> {
		if (this.device) return;
		const handlerName = mediasoupClient.detectDevice();
		console.log('[MediasoupClient] detected device:', handlerName);
		this.device = new mediasoupClient.Device({ handlerName });
		await this.device.load({ routerRtpCapabilities });
	}

	public get rtpCapabilities(): mediasoupClient.types.RtpCapabilities | undefined {
		return this.device?.rtpCapabilities;
	}

	public setOnRemoteStreamAdded(cb: (peerId: string, stream: MediaStream, kind: string) => void): void {
		this.onRemoteStreamAdded = cb;
	}

	public setOnRemoteStreamRemoved(cb: (peerId: string, kind: string) => void): void {
		this.onRemoteStreamRemoved = cb;
	}

	public setOnIceRestarting(cb: (restarting: boolean) => void): void {
		this.onIceRestarting = cb;
	}

	public get hasSendTransport(): boolean {
		return this.sendTransport !== null;
	}

	public get hasRecvTransport(): boolean {
		return this.recvTransport !== null;
	}

	public async createSendTransport(): Promise<void> {
		if (this.sendTransport) return;
		if (!this.device) throw new Error('Device not loaded');

		const info = await signaling.createTransport('send');
		this.sendTransport = this.device.createSendTransport(info);

		this.sendTransport.on('connect', async ({ dtlsParameters }, callback, errback) => {
			try {
				await signaling.connectTransport(info.id, dtlsParameters);
				callback();
			} catch (err) {
				errback(err as Error);
			}
		});

		this.sendTransport.on('produce', async ({ kind, rtpParameters, appData }, callback, errback) => {
			try {
				const result = await signaling.produce({
					transportId: info.id,
					kind,
					rtpParameters,
					appData: appData as Record<string, unknown>
				});
				callback({ id: result.id });
			} catch (err) {
				errback(err as Error);
			}
		});

		this.sendTransport.on('connectionstatechange', (state) => {
			console.log('[MediasoupClient] send transport connection state:', state);
			if (state === 'failed' || state === 'disconnected') {
				this.handleTransportFailure('send');
			}
		});

		this.sendTransport.on('icegatheringstatechange', (state) => {
			console.log('[MediasoupClient] send transport ICE gathering state:', state);
		});
	}

	public async createRecvTransport(): Promise<void> {
		if (this.recvTransport) return;
		if (!this.device) throw new Error('Device not loaded');

		const info = await signaling.createTransport('recv');
		this.recvTransport = this.device.createRecvTransport(info);

		this.recvTransport.on('connect', async ({ dtlsParameters }, callback, errback) => {
			try {
				await signaling.connectTransport(info.id, dtlsParameters);
				callback();
			} catch (err) {
				errback(err as Error);
			}
		});

		this.recvTransport.on('connectionstatechange', (state) => {
			console.log('[MediasoupClient] recv transport connection state:', state);
			if (state === 'failed' || state === 'disconnected') {
				this.handleTransportFailure('recv');
			}
		});

		this.recvTransport.on('icegatheringstatechange', (state) => {
			console.log('[MediasoupClient] recv transport ICE gathering state:', state);
		});
	}

	public async produceCamera(track: MediaStreamTrack): Promise<string> {
		if (!this.sendTransport) throw new Error('No send transport');
		this.cameraProducer = await this.sendTransport.produce({
			track,
			appData: { source: 'camera' }
		});
		return this.cameraProducer.id;
	}

	public async produceMic(track: MediaStreamTrack): Promise<string> {
		if (!this.sendTransport) throw new Error('No send transport');
		this.micProducer = await this.sendTransport.produce({
			track,
			appData: { source: 'mic' }
		});
		return this.micProducer.id;
	}

	public async produceScreen(track: MediaStreamTrack): Promise<string> {
		if (!this.sendTransport) throw new Error('No send transport');
		this.screenProducer = await this.sendTransport.produce({
			track,
			appData: { source: 'screen' }
		});
		return this.screenProducer.id;
	}

	public stopCameraProducer(): void {
		this.cameraProducer?.close();
		this.cameraProducer = null;
	}

	public stopScreenProducer(): void {
		this.screenProducer?.close();
		this.screenProducer = null;
	}

	public pauseCameraProducer(): void {
		this.cameraProducer?.pause();
	}

	public resumeCameraProducer(): void {
		this.cameraProducer?.resume();
	}

	public pauseMicProducer(): void {
		this.micProducer?.pause();
	}

	public resumeMicProducer(): void {
		this.micProducer?.resume();
	}

	public async consumeProducer(producerInfo: ProducerInfo): Promise<void> {
		if (!this.recvTransport) throw new Error('No recv transport');
		if (!this.rtpCapabilities) throw new Error('No rtp capabilities');
		if (this.consumers.has(producerInfo.producerId)) return;

		const result = await signaling.consume({
			producerPeerId: producerInfo.peerId,
			producerId: producerInfo.producerId,
			rtpCapabilities: this.rtpCapabilities
		});

		const consumer = await this.recvTransport.consume({
			id: result.id,
			producerId: producerInfo.producerId,
			kind: result.kind,
			rtpParameters: result.rtpParameters
		});

		this.consumers.set(producerInfo.producerId, consumer);

		let stream = this.remoteStreams.get(producerInfo.peerId);
		if (!stream) {
			stream = new MediaStream();
			this.remoteStreams.set(producerInfo.peerId, stream);
		}
		stream.addTrack(consumer.track);

		this.onRemoteStreamAdded?.(producerInfo.peerId, stream, result.kind);

		consumer.on('transportclose', () => {
			this.consumers.delete(producerInfo.producerId);
			stream?.removeTrack(consumer.track);
		});

		consumer.on('producerclose', () => {
			this.consumers.delete(producerInfo.producerId);
			stream?.removeTrack(consumer.track);
			if (stream && stream.getTracks().length === 0) {
				this.remoteStreams.delete(producerInfo.peerId);
			}
			this.onRemoteStreamRemoved?.(producerInfo.peerId, result.kind);
		});
	}

	public closeConsumer(producerId: string): void {
		const consumer = this.consumers.get(producerId);
		if (consumer) {
			consumer.close();
			this.consumers.delete(producerId);
		}
	}

	public getRemoteStream(peerId: string): MediaStream | undefined {
		return this.remoteStreams.get(peerId);
	}

	private async handleTransportFailure(direction: 'send' | 'recv'): Promise<void> {
		console.warn('[MediasoupClient] ' + direction + ' transport failed, attempting ICE restart');
		await this.restartIce();
	}

	public async restartIce(): Promise<void> {
		if (this.iceRestartInProgress) {
			console.log('[MediasoupClient] ICE restart already in progress, skipping');
			return;
		}

		if (this.consecutiveRestartFailures >= this.MAX_RESTART_FAILURES) {
			console.error('[MediasoupClient] Too many consecutive ICE restart failures, giving up');
			return;
		}

		this.iceRestartInProgress = true;
		this.onIceRestarting?.(true);

		const timeoutPromise = new Promise<never>((_, reject) =>
			setTimeout(
				() => reject(new Error('ICE restart timed out after ' + this.ICE_RESTART_TIMEOUT_MS + 'ms')),
				this.ICE_RESTART_TIMEOUT_MS
			)
		);

		try {
			const transports: { transport: mediasoupClient.types.Transport; direction: 'send' | 'recv' }[] = [];
			if (this.sendTransport) transports.push({ transport: this.sendTransport, direction: 'send' });
			if (this.recvTransport) transports.push({ transport: this.recvTransport, direction: 'recv' });

			await Promise.race([
				Promise.all(
					transports.map(async ({ transport, direction }) => {
						const remoteIceParameters = await signaling.restartIce(transport.id);

						await transport.restartIce(remoteIceParameters);

						console.log('[MediasoupClient] ' + direction + ' transport ICE restart completed');
					})
				),
				timeoutPromise
			]);

			this.consecutiveRestartFailures = 0;
			console.log('[MediasoupClient] ICE restart completed successfully');
		} catch (err) {
			this.consecutiveRestartFailures++;
			console.error('[MediasoupClient] ICE restart failed:', err);
			throw err;
		} finally {
			this.iceRestartInProgress = false;
			this.onIceRestarting?.(false);
		}
	}

	public close(): void {
		this.sendTransport?.close();
		this.recvTransport?.close();
		this.cameraProducer?.close();
		this.micProducer?.close();
		this.screenProducer?.close();
		for (const consumer of this.consumers.values()) consumer.close();
		this.consumers.clear();
		this.remoteStreams.clear();
		this.sendTransport = null;
		this.recvTransport = null;
		this.cameraProducer = null;
		this.micProducer = null;
		this.screenProducer = null;
		this.device = null;
	}
}
