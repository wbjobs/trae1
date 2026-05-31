import React, { useState } from 'react';
import { signaling } from './signaling';

interface LobbyProps {
	onJoinMeeting: (ctx: {
		code: string;
		peerId: string;
		name: string;
		routerRtpCapabilities: mediasoupClient.types.RtpCapabilities;
	}) => void;
}

export const Lobby: React.FC<LobbyProps> = ({ onJoinMeeting }) => {
	const [name, setName] = useState('');
	const [inviteCode, setInviteCode] = useState('');
	const [loading, setLoading] = useState(false);
	const [error, setError] = useState('');

	const handleCreateRoom = async () => {
		if (!name.trim()) {
			setError('请输入昵称');
			return;
		}
		setLoading(true);
		setError('');
		try {
			await signaling.connect();
			const result = await signaling.createRoom(name.trim());
			onJoinMeeting({ ...result, name: name.trim() });
		} catch (err: any) {
			setError(err.message || '创建房间失败');
		} finally {
			setLoading(false);
		}
	};

	const handleJoinRoom = async () => {
		if (!name.trim()) {
			setError('请输入昵称');
			return;
		}
		if (inviteCode.trim().length !== 6) {
			setError('请输入6位邀请码');
			return;
		}
		setLoading(true);
		setError('');
		try {
			await signaling.connect();
			const result = await signaling.joinRoom(inviteCode.trim(), name.trim());
			onJoinMeeting({ ...result, name: name.trim() });
		} catch (err: any) {
			setError(err.message || '加入房间失败');
		} finally {
			setLoading(false);
		}
	};

	return (
		<div className="lobby-container">
			<div className="lobby-card">
				<h1 className="lobby-title">WebRTC 视频会议</h1>
				<p className="lobby-subtitle">基于 mediasoup 的 SFU 视频会议系统</p>

				<div className="input-group">
					<label className="input-label">昵称</label>
					<input
						type="text"
						className="input-field"
						placeholder="请输入你的昵称"
						value={name}
						onChange={(e) => setName(e.target.value)}
						disabled={loading}
						maxLength={20}
					/>
				</div>

				<div className="input-group">
					<label className="input-label">邀请码 (加入房间时填写)</label>
					<input
						type="text"
						className="input-field"
						placeholder="6位数字邀请码"
						value={inviteCode}
						onChange={(e) => setInviteCode(e.target.value.replace(/\D/g, ''))}
						disabled={loading}
						maxLength={6}
					/>
				</div>

				{error && (
					<div style={{ color: '#ef4444', fontSize: 13, marginBottom: 12 }}>{error}</div>
				)}

				<div className="button-row">
					<button
						className="btn btn-primary"
						onClick={handleCreateRoom}
						disabled={loading}
					>
						{loading ? '处理中...' : '创建房间'}
					</button>
					<button
						className="btn btn-secondary"
						onClick={handleJoinRoom}
						disabled={loading}
					>
						加入房间
					</button>
				</div>
			</div>
		</div>
	);
};
