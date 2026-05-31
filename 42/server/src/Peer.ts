import * as mediasoup from 'mediasoup';

type AppData = Record<string, unknown>;

export class Peer {
	public id: string;
	public name: string;
	public cameraOn = true;
	public micOn = true;
	public screenSharing = false;
	public joinedAt: number;

	public transports = new Map<string, mediasoup.types.Transport<AppData>>();
	public producers = new Map<string, mediasoup.types.Producer<AppData>>();
	public consumers = new Map<string, mediasoup.types.Consumer<AppData>>();

	constructor(id: string, name: string) {
		this.id = id;
		this.name = name;
		this.joinedAt = Date.now();
	}

	public toState() {
		return {
			id: this.id,
			name: this.name,
			cameraOn: this.cameraOn,
			micOn: this.micOn,
			screenSharing: this.screenSharing,
			joinedAt: this.joinedAt
		};
	}

	public close() {
		for (const t of this.transports.values()) t.close();
		for (const c of this.consumers.values()) c.close();
		this.transports.clear();
		this.producers.clear();
		this.consumers.clear();
	}
}
