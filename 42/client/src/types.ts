export interface PeerState {
	id: string;
	name: string;
	cameraOn: boolean;
	micOn: boolean;
	screenSharing: boolean;
	joinedAt: number;
}

export interface ProducerInfo {
	peerId: string;
	producerId: string;
	kind: string;
	paused: boolean;
}

export interface RoomState {
	code: string;
	peers: PeerState[];
	producers: ProducerInfo[];
}
