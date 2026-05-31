import React, { useState, useEffect, useRef, useCallback } from 'react';
import { signaling } from './signaling';
import { MediasoupClient } from './mediasoupClient';
import { Toast } from './Toast';
import type { PeerState, ProducerInfo, RoomState } from './types';

interface MeetingPageProps {
	code: string;
	peerId: string;
	name: string;
	routerRtpCapabilities: mediasoupClient.types.RtpCapabilities;
	onLeave: () => void;
}

interface RemotePeer {
	state: PeerState;
	stream?: MediaStream;
	audioLevel: number;
}

export const MeetingPage: React.FC<MeetingPageProps> = ({ code, peerId, name, routerRtpCapabilities, onLeave }) => {
	const [peers, setPeers] = useState<Map<string, RemotePeer>>(new Map());
	const [cameraOn, setCameraOn] = useState(true);
	const [micOn, setMicOn] = useState(true);
	const [screenSharing, setScreenSharing] = useState(false);
	const [iceRestarting, setIceRestarting] = useState(false);
	const [networkSwitchToast, setNetworkSwitchToast] = useState(false);
	const [localStream, setLocalStream] = useState<MediaStream | null>(null);
	const [localAudioLevel, setLocalAudioLevel] = useState(0);
	const [error, setError] = useState('');

	const [isRecording, setIsRecording] = useState(false);
	const [recordingStartTime, setRecordingStartTime] = useState(0);
	const [recordingDuration, setRecordingDuration] = useState(0);
	const [lastRecording, setLastRecording] = useState<{ id: string; shareUrl: string; duration: number } | null>(null);
	const [showShareLink, setShowShareLink] = useState(false);
	const [copiedLink, setCopiedLink] = useState(false);

	const videoRefs = useRef<Map<string, HTMLVideoElement>>(new Map());
	const localVideoRef = useRef<HTMLVideoElement>(null);
	const msClientRef = useRef<MediasoupClient | null>(null);
	const audioContextRef = useRef<AudioContext | null>(null);
	const analyserRef = useRef<AnalyserNode | null>(null);
	const remoteAnalysersRef = useRef<Map<string, { analyser: AnalyserNode; source: MediaStreamAudioSourceNode }>>(new Map());
	const localStreamRef = useRef<MediaStream | null>(null);
	const screenStreamRef = useRef<MediaStream | null>(null);
	const networkCheckTimerRef = useRef<number | null>(null);
	const recordingTimerRef = useRef<number | null>(null);

	const startLocalAudioAnalysis = useCallback((stream: MediaStream) => {
		const audioTrack = stream.getAudioTracks()[0];
		if (!audioTrack) return;

		const audioContext = new (window.AudioContext || (window as any).webkitAudioContext)();
		const source = audioContext.createMediaStreamSource(stream);
		const analyser = audioContext.createAnalyser();
		analyser.fftSize = 256;
		source.connect(analyser);

		audioContextRef.current = audioContext;
		analyserRef.current = analyser;

		const dataArray = new Uint8Array(analyser.frequencyBinCount);
		const updateLevel = () => {
			if (!analyserRef.current) return;
			analyserRef.current.getByteFrequencyData(dataArray);
			let sum = 0;
			for (let i = 0; i < dataArray.length; i++) sum += dataArray[i];
			const avg = sum / dataArray.length / 255;
			setLocalAudioLevel(avg);
			requestAnimationFrame(updateLevel);
		};
		updateLevel();
	}, []);

	const startRemoteAudioAnalysis = useCallback((peerId: string, stream: MediaStream) => {
		const audioTrack = stream.getAudioTracks()[0];
		if (!audioTrack) return;

		const audioContext = new (window.AudioContext || (window as any).webkitAudioContext)();
		const source = audioContext.createMediaStreamSource(stream);
		const analyser = audioContext.createAnalyser();
		analyser.fftSize = 256;
		source.connect(analyser);

		remoteAnalysersRef.current.set(peerId, { analyser, source });

		const dataArray = new Uint8Array(analyser.frequencyBinCount);
		const updateLevel = () => {
			const entry = remoteAnalysersRef.current.get(peerId);
			if (!entry) return;
			entry.analyser.getByteFrequencyData(dataArray);
			let sum = 0;
			for (let i = 0; i < dataArray.length; i++) sum += dataArray[i];
			const avg = sum / dataArray.length / 255;

			setPeers((prev) => {
				const next = new Map(prev);
				const existing = next.get(peerId);
				if (existing) {
					next.set(peerId, { ...existing, audioLevel: avg });
				}
				return next;
			});
			requestAnimationFrame(updateLevel);
		};
		updateLevel();
	}, []);

	useEffect(() => {
		let mounted = true;
		const msClient = new MediasoupClient();
		msClientRef.current = msClient;

		msClient.setOnIceRestarting((restarting) => {
			setIceRestarting(restarting);
			setNetworkSwitchToast(restarting);
			if (restarting) {
				console.log('[MeetingPage] ICE restart initiated, showing network switch toast');
			}
		});

		msClient.setOnRemoteStreamAdded((pid, stream, kind) => {
			if (!mounted) return;
			if (kind === 'audio') {
				startRemoteAudioAnalysis(pid, stream);
			}
			setPeers((prev) => {
				const next = new Map(prev);
				const existing = next.get(pid);
				if (existing) {
					next.set(pid, { ...existing, stream });
				} else {
					next.set(pid, { state: { id: pid, name: 'Unknown', cameraOn: true, micOn: true, screenSharing: false, joinedAt: Date.now() }, stream, audioLevel: 0 });
				}
				return next;
			});
		});

		msClient.setOnRemoteStreamRemoved((pid) => {
			if (!mounted) return;
			const entry = remoteAnalysersRef.current.get(pid);
			if (entry) {
				entry.source.disconnect();
				entry.analyser.disconnect();
				remoteAnalysersRef.current.delete(pid);
			}
		});

		const init = async () => {
			try {
				await msClient.load(routerRtpCapabilities);
				await msClient.createSendTransport();
				await msClient.createRecvTransport();

				const stream = await navigator.mediaDevices.getUserMedia({
					video: true,
					audio: true
				});
				localStreamRef.current = stream;
				setLocalStream(stream);
				startLocalAudioAnalysis(stream);

				const videoTrack = stream.getVideoTracks()[0];
				const audioTrack = stream.getAudioTracks()[0];

				await msClient.produceCamera(videoTrack);
				await msClient.produceMic(audioTrack);

				signaling.updateMediaState({ cameraOn: true, micOn: true, screenSharing: false });
			} catch (err: any) {
				setError(err.message || '初始化失败');
			}
		};

		init();

		const offRoomState = signaling.on('room:state', (data) => {
			const state = data as RoomState;
			setPeers((prev) => {
				const next = new Map(prev);
				for (const p of state.peers) {
					if (p.id === peerId) continue;
					const existing = next.get(p.id);
					next.set(p.id, {
						state: p,
						stream: existing?.stream,
						audioLevel: existing?.audioLevel ?? 0
					});
				}
				for (const id of [...next.keys()]) {
					if (!state.peers.find((p) => p.id === id)) {
						next.delete(id);
					}
				}
				return next;
			});
		});

		const offPeerJoined = signaling.on('peer:joined', (data) => {
			const peer = data as PeerState;
			if (peer.id === peerId) return;
			setPeers((prev) => {
				const next = new Map(prev);
				next.set(peer.id, { state: peer, audioLevel: 0 });
				return next;
			});
		});

		const offPeerLeft = signaling.on('peer:left', (data) => {
			const { peerId: pid } = data as { peerId: string };
			setPeers((prev) => {
				const next = new Map(prev);
				next.delete(pid);
				return next;
			});
			const entry = remoteAnalysersRef.current.get(pid);
			if (entry) {
				entry.source.disconnect();
				entry.analyser.disconnect();
				remoteAnalysersRef.current.delete(pid);
			}
		});

		const offPeerStateChanged = signaling.on('peer:state-changed', (data) => {
			const peer = data as PeerState;
			if (peer.id === peerId) return;
			setPeers((prev) => {
				const next = new Map(prev);
				const existing = next.get(peer.id);
				if (existing) {
					next.set(peer.id, { ...existing, state: peer });
				}
				return next;
			});
		});

		const offProducerAdded = signaling.on('producer:added', async (data) => {
			const info = data as ProducerInfo;
			if (info.peerId === peerId) return;
			try {
				await msClient.consumeProducer(info);
			} catch (err) {
				console.error('Failed to consume producer:', err);
			}
		});

		const offProducerRemoved = signaling.on('producer:removed', (data) => {
			const { producerId } = data as { producerId: string };
			msClient.closeConsumer(producerId);
		});

		const offRecordingStarted = signaling.on('recording:started', (data) => {
			const info = data as { recordingId: string; startedAt: number };
			setIsRecording(true);
			setRecordingStartTime(info.startedAt);
			setRecordingDuration(0);
			setShowShareLink(false);
			setLastRecording(null);

			recordingTimerRef.current = window.setInterval(() => {
				setRecordingDuration(Math.floor((Date.now() - info.startedAt) / 1000));
			}, 1000);
		});

		const offRecordingStopped = signaling.on('recording:stopped', (data) => {
			const info = data as { recordingId: string; shareUrl: string; duration: number };
			setIsRecording(false);
			setRecordingStartTime(0);
			setRecordingDuration(0);
			setLastRecording({ id: info.recordingId, shareUrl: info.shareUrl, duration: info.duration });
			setShowShareLink(true);

			if (recordingTimerRef.current) {
				clearInterval(recordingTimerRef.current);
				recordingTimerRef.current = null;
			}
		});

		const handleOnline = () => {
			console.log('[MeetingPage] Network online event detected, triggering ICE restart');
			msClient.restartIce().catch((err) => {
				console.error('[MeetingPage] ICE restart after online failed:', err);
			});
		};

		const handleOffline = () => {
			console.log('[MeetingPage] Network offline event detected');
			setNetworkSwitchToast(true);
			window.setTimeout(() => {
				if (navigator.onLine) {
					setNetworkSwitchToast(false);
				}
			}, 5000);
		};

		const handleConnectionChange = () => {
			const connection = (navigator as any).connection;
			if (connection) {
				console.log('[MeetingPage] Network connection changed: effectiveType=' + connection.effectiveType + ', type=' + connection.type);
			}
			msClient.restartIce().catch((err) => {
				console.error('[MeetingPage] ICE restart after connection change failed:', err);
			});
		};

		window.addEventListener('online', handleOnline);
		window.addEventListener('offline', handleOffline);

		const connection = (navigator as any).connection || (navigator as any).mozConnection || (navigator as any).webkitConnection;
		if (connection) {
			connection.addEventListener('change', handleConnectionChange);
		}

		const checkNetworkInterval = window.setInterval(() => {
			if (!navigator.onLine) {
				setNetworkSwitchToast(true);
			}
		}, 1000);
		networkCheckTimerRef.current = checkNetworkInterval;

		return () => {
			mounted = false;
			window.removeEventListener('online', handleOnline);
			window.removeEventListener('offline', handleOffline);
			if (connection) {
				connection.removeEventListener('change', handleConnectionChange);
			}
			window.clearInterval(checkNetworkInterval);

			offRoomState();
			offPeerJoined();
			offPeerLeft();
			offPeerStateChanged();
			offProducerAdded();
			offProducerRemoved();
			offRecordingStarted();
			offRecordingStopped();

			if (recordingTimerRef.current) {
				clearInterval(recordingTimerRef.current);
			}

			audioContextRef.current?.close();
			audioContextRef.current = null;

			for (const entry of remoteAnalysersRef.current.values()) {
				entry.source.disconnect();
				entry.analyser.disconnect();
			}
			remoteAnalysersRef.current.clear();

			localStreamRef.current?.getTracks().forEach((t) => t.stop());
			screenStreamRef.current?.getTracks().forEach((t) => t.stop());
			signaling.leaveRoom();
			signaling.disconnect();
			msClient.close();
		};
	}, []);

	useEffect(() => {
		for (const [pid, remote] of peers.entries()) {
			if (remote.stream) {
				const videoEl = videoRefs.current.get(pid);
				if (videoEl && videoEl.srcObject !== remote.stream) {
					videoEl.srcObject = remote.stream;
					videoEl.muted = true;
				}
			}
		}
	}, [peers]);

	useEffect(() => {
		if (localVideoRef.current && localStream) {
			localVideoRef.current.srcObject = localStream;
			localVideoRef.current.muted = true;
		}
	}, [localStream]);

	const toggleCamera = () => {
		const newState = !cameraOn;
		setCameraOn(newState);
		if (localStreamRef.current) {
			const videoTrack = localStreamRef.current.getVideoTracks()[0];
			if (videoTrack) videoTrack.enabled = newState;
		}
		if (msClientRef.current) {
			if (newState) {
				msClientRef.current.resumeCameraProducer();
			} else {
				msClientRef.current.pauseCameraProducer();
			}
		}
		signaling.updateMediaState({ cameraOn: newState, micOn, screenSharing });
	};

	const toggleMic = () => {
		const newState = !micOn;
		setMicOn(newState);
		if (localStreamRef.current) {
			const audioTrack = localStreamRef.current.getAudioTracks()[0];
			if (audioTrack) audioTrack.enabled = newState;
		}
		if (msClientRef.current) {
			if (newState) {
				msClientRef.current.resumeMicProducer();
			} else {
				msClientRef.current.pauseMicProducer();
			}
		}
		signaling.updateMediaState({ cameraOn, micOn: newState, screenSharing });
	};

	const toggleScreenShare = async () => {
		if (!screenSharing) {
			try {
				const screenStream = await navigator.mediaDevices.getDisplayMedia({
					video: true,
					audio: false
				});
				screenStreamRef.current = screenStream;
				setScreenSharing(true);
				await msClientRef.current?.produceScreen(screenStream.getVideoTracks()[0]);
				signaling.updateMediaState({ cameraOn, micOn, screenSharing: true });

				screenStream.getVideoTracks()[0].addEventListener('ended', () => {
					stopScreenShare();
				});
			} catch (err) {
				console.error('Screen share failed:', err);
			}
		} else {
			stopScreenShare();
		}
	};

	const stopScreenShare = () => {
		screenStreamRef.current?.getTracks().forEach((t) => t.stop());
		screenStreamRef.current = null;
		setScreenSharing(false);
		msClientRef.current?.stopScreenProducer();
		signaling.updateMediaState({ cameraOn, micOn, screenSharing: false });
	};

	const handleLeave = () => {
		onLeave();
	};

	const formatRecordingDuration = (seconds: number): string => {
		const m = Math.floor(seconds / 60);
		const s = Math.floor(seconds % 60);
		return `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
	};

	const toggleRecording = async () => {
		try {
			if (!isRecording) {
				const result = await signaling.startRecording();
				if (!result.ok) {
					setError(result.error || '开始录制失败');
				}
			} else {
				const result = await signaling.stopRecording();
				if (!result.ok) {
					setError(result.error || '停止录制失败');
				}
			}
		} catch (err: any) {
			setError(err.message || '录制操作失败');
		}
	};

	const copyShareLink = async () => {
		if (!lastRecording) return;
		try {
			const shareUrl = `${window.location.origin}/replay/${lastRecording.id}`;
			await navigator.clipboard.writeText(shareUrl);
			setCopiedLink(true);
			setTimeout(() => setCopiedLink(false), 2000);
		} catch (_) {
			/* ignore */
		}
	};

	const allPeers = Array.from(peers.values());
	const gridCount = Math.min(4, allPeers.length + 1);

	const renderVolumeBars = (level: number) => {
		const bars = [];
		for (let i = 0; i < 5; i++) {
			const threshold = i * 0.2;
			const height = level > threshold ? Math.min(16, (level - threshold) * 24) : 2;
			bars.push(
				<div
					key={i}
					className="volume-bar"
					style={{ height: `${height}px` }}
				/>
			);
		}
		return <div className="volume-bars">{bars}</div>;
	};

	return (
		<div className="meeting-container">
			{networkSwitchToast && (
				<Toast message={iceRestarting ? '网络切换中...' : '网络连接中...'} loading={iceRestarting} />
			)}

			<div className="meeting-header">
				<div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
					<div className="room-code">
						房间号: <strong>{code}</strong>
					</div>
					{isRecording && (
						<div style={{
							display: 'flex',
							alignItems: 'center',
							gap: 6,
							background: 'rgba(220, 38, 38, 0.2)',
							padding: '4px 12px',
							borderRadius: 16,
							border: '1px solid rgba(220, 38, 38, 0.4)'
						}}>
							<div style={{
								width: 8,
								height: 8,
								borderRadius: '50%',
								background: '#dc2626',
								animation: 'pulse 1.5s ease-in-out infinite'
							}} />
							<span style={{ color: '#dc2626', fontSize: 13, fontWeight: 600 }}>
								REC {formatRecordingDuration(recordingDuration)}
							</span>
						</div>
					)}
				</div>
				<div style={{ fontSize: 13, color: '#a1a1aa' }}>
					{name} ({allPeers.length + 1}/4)
				</div>
			</div>

			<div className={`video-grid grid-${gridCount}`}>
				<div className="video-tile">
					<video ref={localVideoRef} autoPlay playsInline />
					<div className="video-overlay">
						<span className="video-name">{name} (你)</span>
						<div className="mic-indicator">
							<span className={micOn ? 'mic-active' : 'mic-muted'}>
								{micOn ? '🎤' : '🔇'}
							</span>
							{micOn && renderVolumeBars(localAudioLevel)}
						</div>
					</div>
					{!cameraOn && <div className="video-placeholder">📷</div>}
				</div>

				{allPeers.map((peer) => (
					<div key={peer.state.id} className="video-tile">
						<video
							ref={(el) => {
								if (el) videoRefs.current.set(peer.state.id, el);
							}}
							autoPlay
							playsInline
						/>
						{peer.state.screenSharing && (
							<div className="screen-share-tag">🖥 屏幕共享</div>
						)}
						<div className="video-overlay">
							<span className="video-name">{peer.state.name}</span>
							<div className="mic-indicator">
								<span className={peer.state.micOn ? 'mic-active' : 'mic-muted'}>
									{peer.state.micOn ? '🎤' : '🔇'}
								</span>
								{peer.state.micOn && renderVolumeBars(peer.audioLevel)}
							</div>
						</div>
						{!peer.state.cameraOn && !peer.state.screenSharing && (
							<div className="video-placeholder">👤</div>
						)}
					</div>
				))}
			</div>

			{error && (
				<div style={{
					position: 'absolute', top: 60, left: '50%', transform: 'translateX(-50%)',
					background: '#dc2626', color: 'white', padding: '8px 16px', borderRadius: 8, zIndex: 100
				}}>
					{error}
				</div>
			)}

			{showShareLink && lastRecording && (
				<div style={{
					position: 'fixed',
					top: 0,
					left: 0,
					right: 0,
					bottom: 0,
					background: 'rgba(0,0,0,0.6)',
					display: 'flex',
					alignItems: 'center',
					justifyContent: 'center',
					zIndex: 1000
				}} onClick={() => setShowShareLink(false)}>
					<div style={{
						background: '#1e1e2a',
						borderRadius: 16,
						padding: 32,
						maxWidth: 480,
						width: '90%',
						boxShadow: '0 8px 32px rgba(0,0,0,0.4)'
					}} onClick={(e) => e.stopPropagation()}>
						<div style={{ textAlign: 'center', marginBottom: 20 }}>
							<div style={{ fontSize: 40, marginBottom: 8 }}>✅</div>
							<h3 style={{ color: '#e4e4e7', fontSize: 18, marginBottom: 4 }}>录制已完成</h3>
							<p style={{ color: '#a1a1aa', fontSize: 14 }}>
								时长: {formatRecordingDuration(lastRecording.duration)}
							</p>
						</div>
						<div style={{ marginBottom: 20 }}>
							<div style={{ fontSize: 13, color: '#71717a', marginBottom: 6 }}>分享链接</div>
							<div style={{
								display: 'flex',
								gap: 8,
								background: '#27273a',
								borderRadius: 8,
								padding: 12
							}}>
								<input
									type="text"
									value={`${window.location.origin}/replay/${lastRecording.id}`}
									readOnly
									style={{
										flex: 1,
										background: 'transparent',
										border: 'none',
										color: '#e4e4e7',
										fontSize: 13,
										outline: 'none'
									}}
								/>
								<button
									onClick={copyShareLink}
									style={{
										background: copiedLink ? '#22c55e' : '#818cf8',
										border: 'none',
										borderRadius: 6,
										color: 'white',
										fontSize: 13,
										padding: '6px 14px',
										cursor: 'pointer',
										transition: 'all 0.2s'
									}}
								>
									{copiedLink ? '已复制' : '复制'}
								</button>
							</div>
						</div>
						<div style={{ display: 'flex', gap: 12 }}>
							<button
								onClick={() => setShowShareLink(false)}
								style={{
									flex: 1,
									padding: '10px',
									background: '#3f3f52',
									border: 'none',
									borderRadius: 8,
									color: '#e4e4e7',
									fontSize: 14,
									cursor: 'pointer'
								}}
							>
								关闭
							</button>
							<button
								onClick={() => {
									window.open(`/replay/${lastRecording.id}`, '_blank');
								}}
								style={{
									flex: 1,
									padding: '10px',
									background: 'linear-gradient(135deg, #818cf8, #6366f1)',
									border: 'none',
									borderRadius: 8,
									color: 'white',
									fontSize: 14,
									fontWeight: 600,
									cursor: 'pointer'
								}}
							>
								查看回放
							</button>
						</div>
					</div>
				</div>
			)}

			<div className="controls-bar">
				<button
					className={`control-btn ${isRecording ? 'active off' : ''}`}
					onClick={toggleRecording}
					title={isRecording ? '停止录制' : '开始录制'}
					style={isRecording ? { background: '#dc2626' } : {}}
				>
					{isRecording ? '⏹' : '⏺'}
				</button>
				<button
					className={`control-btn ${cameraOn ? 'active' : 'active off'}`}
					onClick={toggleCamera}
					title={cameraOn ? '关闭摄像头' : '开启摄像头'}
				>
					{cameraOn ? '📹' : '🚫'}
				</button>
				<button
					className={`control-btn ${micOn ? 'active' : 'active off'}`}
					onClick={toggleMic}
					title={micOn ? '静音麦克风' : '取消静音'}
				>
					{micOn ? '🎤' : '🔇'}
				</button>
				<button
					className={`control-btn ${screenSharing ? 'active' : ''}`}
					onClick={toggleScreenShare}
					title={screenSharing ? '停止屏幕共享' : '开始屏幕共享'}
				>
					{screenSharing ? '🖥' : '📺'}
				</button>
				<button className="control-btn leave" onClick={handleLeave}>
					离开
				</button>
			</div>
		</div>
	);
};
