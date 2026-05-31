import * as mediasoup from 'mediasoup';
import { randomInt } from 'crypto';
import { Peer } from './Peer';
import { INVITE_CODE_LENGTH, MAX_PARTICIPANTS_PER_ROOM, routerOptions, webRtcTransportOptions } from './config';

type AppData = Record<string, unknown>;

export interface RoomEvents {
	onStateChange: (room: Room) => void;
	onDestroyed: (room: Room) => void;
	onPeerJoined: (room: Room, peer: Peer) => void;
	onPeerLeft: (room: Room, peer: Peer) => void;
	onProducerAdded: (room: Room, peer: Peer, producer: mediasoup.types.Producer<AppData>) => void;
	onProducerRemoved: (room: Room, peer: Peer, producerId: string) => void;
	onPeerStateChanged: (room: Room, peer: Peer) => void;
}

export class Room {
	public code: string;
	public createdAt: number;
	public router: mediasoup.types.Router<AppData>;
	public webRtcServer: mediasoup.types.WebRtcServer<AppData>;

	public peers = new Map<string, Peer>();
	private events: RoomEvents;

	constructor(params: {
		router: mediasoup.types.Router<AppData>;
		webRtcServer: mediasoup.types.WebRtcServer<AppData>;
		events: RoomEvents;
	}) {
		this.code = Room.generateInviteCode();
		this.createdAt = Date.now();
		this.router = params.router;
		this.webRtcServer = params.webRtcServer;
		this.events = params.events;
	}

	public static generateInviteCode(): string {
		return randomInt(0, Math.pow(10, INVITE_CODE_LENGTH))
			.toString()
			.padStart(INVITE_CODE_LENGTH, '0');
	}

	public get routerRtpCapabilities(): mediasoup.types.RtpCapabilities {
		return this.router.rtpCapabilities;
	}

	public hasReachedCapacity(): boolean {
		return this.peers.size >= MAX_PARTICIPANTS_PER_ROOM;
	}

	public addPeer(peer: Peer): void {
		if (this.hasReachedCapacity()) {
			throw new Error('Room is full');
		}
		this.peers.set(peer.id, peer);
		this.events.onPeerJoined(this, peer);
		this.events.onStateChange(this);
	}

	public removePeer(peerId: string): void {
		const peer = this.peers.get(peerId);
		if (!peer) return;
		for (const producer of peer.producers.values()) {
			this.events.onProducerRemoved(this, peer, producer.id);
			this.closeProducersFor(producer.id);
		}
		peer.close();
		this.peers.delete(peerId);
		this.events.onPeerLeft(this, peer);
		this.events.onStateChange(this);
	}

	public getPeer(peerId: string): Peer | undefined {
		return this.peers.get(peerId);
	}

	public setPeerMediaState(
		peerId: string,
		changes: Partial<Pick<Peer, 'cameraOn' | 'micOn' | 'screenSharing'>>
	): void {
		const peer = this.peers.get(peerId);
		if (!peer) return;
		if (changes.cameraOn !== undefined) peer.cameraOn = changes.cameraOn;
		if (changes.micOn !== undefined) peer.micOn = changes.micOn;
		if (changes.screenSharing !== undefined) peer.screenSharing = changes.screenSharing;
		this.events.onPeerStateChanged(this, peer);
		this.events.onStateChange(this);
	}

	public async createWebRtcTransport(peerId: string, direction: 'send' | 'recv'): Promise<{
		id: string;
		iceParameters: mediasoup.types.IceParameters;
		iceCandidates: mediasoup.types.IceCandidate[];
		dtlsParameters: mediasoup.types.DtlsParameters;
	}> {
		const peer = this.peers.get(peerId);
		if (!peer) throw new Error('Peer not found');

		const transport = await this.router.createWebRtcTransport({
			webRtcServer: this.webRtcServer,
			...webRtcTransportOptions,
			appData: { peerId, direction } as AppData
		});

		transport.on('dtlsstatechange', (dtlsState) => {
			if (dtlsState === 'failed' || dtlsState === 'closed') {
				transport.close();
			}
		});

		transport.on('close', () => {
			peer.transports.delete(transport.id);
			for (const producer of [...peer.producers.values()]) {
				if (producer.transport && producer.transport.id === transport.id) {
					peer.producers.delete(producer.id);
					this.events.onProducerRemoved(this, peer, producer.id);
					this.closeProducersFor(producer.id);
				}
			}
		});

		peer.transports.set(transport.id, transport);
		return {
			id: transport.id,
			iceParameters: transport.iceParameters,
			iceCandidates: transport.iceCandidates,
			dtlsParameters: transport.dtlsParameters
		};
	}

	public async connectTransport(transportId: string, dtlsParameters: mediasoup.types.DtlsParameters, peerId: string): Promise<void> {
		const peer = this.peers.get(peerId);
		if (!peer) throw new Error('Peer not found');
		const transport = peer.transports.get(transportId);
		if (!transport) throw new Error('Transport not found');
		await transport.connect({ dtlsParameters });
	}

	public async produce(params: {
		peerId: string;
		transportId: string;
		kind: mediasoup.types.MediaKind;
		rtpParameters: mediasoup.types.RtpParameters;
		appData?: AppData;
	}): Promise<{ id: string }> {
		const peer = this.peers.get(params.peerId);
		if (!peer) throw new Error('Peer not found');
		const transport = peer.transports.get(params.transportId);
		if (!transport) throw new Error('Transport not found');

		const producer = await transport.produce({
			kind: params.kind,
			rtpParameters: params.rtpParameters,
			appData: { peerId: peer.id, ...(params.appData || {}) } as AppData
		});

		producer.on('close', () => {
			if (peer.producers.has(producer.id)) {
				peer.producers.delete(producer.id);
				this.events.onProducerRemoved(this, peer, producer.id);
				this.closeProducersFor(producer.id);
			}
		});

		peer.producers.set(producer.id, producer);
		this.events.onProducerAdded(this, peer, producer);
		this.events.onStateChange(this);
		return { id: producer.id };
	}

	public async consume(params: {
		consumerPeerId: string;
		producerPeerId: string;
		producerId: string;
		rtpCapabilities: mediasoup.types.RtpCapabilities;
	}): Promise<{
		id: string;
		kind: mediasoup.types.MediaKind;
		rtpParameters: mediasoup.types.RtpParameters;
		producerPaused: boolean;
	}> {
		const consumerPeer = this.peers.get(params.consumerPeerId);
		if (!consumerPeer) throw new Error('Consumer peer not found');

		let recvTransport: mediasoup.types.Transport<AppData> | undefined;
		for (const t of consumerPeer.transports.values()) {
			const appData = t.appData as { direction?: string };
			if (appData.direction === 'recv') {
				recvTransport = t;
				break;
			}
		}
		if (!recvTransport) throw new Error('No recv transport available');

		if (!this.router.canConsume({ producerId: params.producerId, rtpCapabilities: params.rtpCapabilities })) {
			throw new Error('Cannot consume producer');
		}

		const consumer = await recvTransport.consume({
			producerId: params.producerId,
			rtpCapabilities: params.rtpCapabilities,
			paused: false
		});

		consumer.on('transportclose', () => {
			consumerPeer.consumers.delete(consumer.id);
		});

		consumer.on('producerclose', () => {
			consumer.close();
			consumerPeer.consumers.delete(consumer.id);
		});

		consumerPeer.consumers.set(consumer.id, consumer);

		return {
			id: consumer.id,
			kind: consumer.kind,
			rtpParameters: consumer.rtpParameters,
			producerPaused: consumer.producerPaused
		};
	}

	private closeProducersFor(producerId: string): void {
		for (const peer of this.peers.values()) {
			for (const consumer of [...peer.consumers.values()]) {
				if (consumer.producerId === producerId) {
					consumer.close();
					peer.consumers.delete(consumer.id);
				}
			}
		}
	}

	public async restartTransportIce(transportId: string, peerId: string): Promise<mediasoup.types.IceParameters> {
		const peer = this.peers.get(peerId);
		if (!peer) throw new Error('Peer not found');
		const transport = peer.transports.get(transportId);
		if (!transport) throw new Error('Transport not found');
		const iceParameters = await transport.restartIce();
		return iceParameters;
	}

	public close(): void {
		for (const peer of this.peers.values()) peer.close();
		this.peers.clear();
		this.router.close();
		this.events.onDestroyed(this);
	}
}
