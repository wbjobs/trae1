export interface PeerInit {
	id: string;
	name: string;
}

export interface PeerState {
	id: string;
	name: string;
	cameraOn: boolean;
	micOn: boolean;
	screenSharing: boolean;
	joinedAt: number;
}

export interface CreateRoomResult {
	code: string;
}
