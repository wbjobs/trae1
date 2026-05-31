import os from 'os';

export const LISTEN_IP = '0.0.0.0';
export const LISTEN_PORT = Number(process.env.PORT || 3001);

export const mediasoupWorkerRtcMinPort = Number(process.env.MEDIASOUP_MIN_PORT || 40000);
export const mediasoupWorkerRtcMaxPort = Number(process.env.MEDIASOUP_MAX_PORT || 40100);

export const mediasoupWebRtcServerOptions = {
	listenInfos: [
		{
			protocol: 'udp',
			ip: process.env.MEDIASOUP_ANNOUNCED_IP || '127.0.0.1',
			announcedIp: process.env.MEDIASOUP_ANNOUNCED_IP || '127.0.0.1'
		},
		{
			protocol: 'tcp',
			ip: process.env.MEDIASOUP_ANNOUNCED_IP || '127.0.0.1',
			announcedIp: process.env.MEDIASOUP_ANNOUNCED_IP || '127.0.0.1'
		}
	]
};

export const routerOptions = {
	mediaCodecs: [
		{
			mimeType: 'audio/opus',
			clockRate: 48000,
			channels: 2
		},
		{
			mimeType: 'video/VP8',
			clockRate: 90000,
			parameters: {
				'x-google-start-bitrate': 1000
			}
		}
	]
};

export const webRtcTransportOptions = {
	initialAvailableOutgoingBitrate: 1000000,
	minimumAvailableOutgoingBitrate: 600000,
	maxSctpMessageSize: 262144
};

export const MAX_PARTICIPANTS_PER_ROOM = 4;
export const INVITE_CODE_LENGTH = 6;
export const WORKER_NUM = Math.min(4, os.cpus().length);
