import React, { useState, useEffect, useCallback } from 'react';
import { RecorderPlayer } from './RecorderPlayer';

interface ReplayPageProps {
	recordingId: string;
	onBack: () => void;
}

interface RecordingMeta {
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

export const ReplayPage: React.FC<ReplayPageProps> = ({ recordingId, onBack }) => {
	const [meta, setMeta] = useState<RecordingMeta | null>(null);
	const [loading, setLoading] = useState(true);
	const [error, setError] = useState('');

	const fetchMeta = useCallback(async () => {
		try {
			const response = await fetch(`/api/recordings/${recordingId}`);
			if (!response.ok) {
				throw new Error('录制文件不存在或已过期');
			}
			const data = await response.json();
			setMeta(data);
		} catch (err: any) {
			setError(err.message || '加载失败');
		} finally {
			setLoading(false);
		}
	}, [recordingId]);

	useEffect(() => {
		fetchMeta();
	}, [fetchMeta]);

	const formatDateTime = (timestamp: number): string => {
		const date = new Date(timestamp);
		return date.toLocaleString('zh-CN', {
			year: 'numeric',
			month: '2-digit',
			day: '2-digit',
			hour: '2-digit',
			minute: '2-digit'
		});
	};

	const formatDuration = (seconds: number): string => {
		const m = Math.floor(seconds / 60);
		const s = Math.floor(seconds % 60);
		return `${m}分${s.toString().padStart(2, '0')}秒`;
	};

	const formatFileSize = (bytes: number): string => {
		if (bytes < 1024) return bytes + ' B';
		if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
		return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
	};

	if (loading) {
		return (
			<div style={{
				display: 'flex',
				justifyContent: 'center',
				alignItems: 'center',
				minHeight: '100vh',
				background: '#0f0f14'
			}}>
				<div style={{ textAlign: 'center', color: '#a1a1aa' }}>
					<div style={{
						width: 40,
						height: 40,
						border: '3px solid #3f3f52',
						borderTopColor: '#818cf8',
						borderRadius: '50%',
						animation: 'spin 0.8s linear infinite',
						margin: '0 auto 16px'
					}} />
					<p>加载中...</p>
				</div>
			</div>
		);
	}

	if (error) {
		return (
			<div style={{
				display: 'flex',
				justifyContent: 'center',
				alignItems: 'center',
				minHeight: '100vh',
				background: '#0f0f14',
				padding: 20
			}}>
				<div style={{
					background: '#1e1e2a',
					padding: 40,
					borderRadius: 16,
					textAlign: 'center',
					maxWidth: 400
				}}>
					<div style={{ fontSize: 48, marginBottom: 16 }}>😔</div>
					<h2 style={{ color: '#e4e4e7', marginBottom: 12 }}>无法播放</h2>
					<p style={{ color: '#a1a1aa', marginBottom: 24 }}>{error}</p>
					<button
						onClick={onBack}
						style={{
							padding: '10px 24px',
							background: 'linear-gradient(135deg, #818cf8, #6366f1)',
							border: 'none',
							borderRadius: 8,
							color: 'white',
							fontSize: 14,
							cursor: 'pointer'
						}}
					>
						返回
					</button>
				</div>
			</div>
		);
	}

	const videoSrc = meta?.url || `/api/recordings/${recordingId}`;
	const expiresInDays = meta ? Math.ceil((meta.expiresAt - Date.now()) / (1000 * 60 * 60 * 24)) : 0;

	return (
		<div style={{
			minHeight: '100vh',
			background: '#0f0f14',
			padding: '40px 20px'
		}}>
			<div style={{ maxWidth: 960, margin: '0 auto' }}>
				<div style={{
					display: 'flex',
					justifyContent: 'space-between',
					alignItems: 'center',
					marginBottom: 24
				}}>
					<button
						onClick={onBack}
						style={{
							background: 'none',
							border: 'none',
							color: '#a1a1aa',
							cursor: 'pointer',
							fontSize: 14,
							padding: '8px 12px'
						}}
					>
						← 返回
					</button>
					<h1 style={{ color: '#e4e4e7', fontSize: 20, fontWeight: 600 }}>
						会议录制回放
					</h1>
					<div style={{ width: 60 }} />
				</div>

				<RecorderPlayer src={videoSrc} />

				{meta && (
					<div style={{
						marginTop: 24,
						background: '#1e1e2a',
						borderRadius: 12,
						padding: 24
					}}>
						<div style={{
							display: 'grid',
							gridTemplateColumns: '1fr 1fr',
							gap: 16
						}}>
							<div>
								<div style={{ fontSize: 12, color: '#71717a', marginBottom: 4 }}>房间号</div>
								<div style={{ fontSize: 15, color: '#818cf8', fontFamily: 'monospace' }}>
									{meta.roomCode}
								</div>
							</div>
							<div>
								<div style={{ fontSize: 12, color: '#71717a', marginBottom: 4 }}>录制时长</div>
								<div style={{ fontSize: 15, color: '#e4e4e7' }}>
									{formatDuration(meta.duration)}
								</div>
							</div>
							<div>
								<div style={{ fontSize: 12, color: '#71717a', marginBottom: 4 }}>开始时间</div>
								<div style={{ fontSize: 15, color: '#e4e4e7' }}>
									{formatDateTime(meta.startedAt)}
								</div>
							</div>
							<div>
								<div style={{ fontSize: 12, color: '#71717a', marginBottom: 4 }}>文件大小</div>
								<div style={{ fontSize: 15, color: '#e4e4e7' }}>
									{formatFileSize(meta.fileSize)}
								</div>
							</div>
							<div style={{ gridColumn: '1 / -1' }}>
								<div style={{ fontSize: 12, color: '#71717a', marginBottom: 4 }}>有效期</div>
								<div style={{ fontSize: 15, color: '#e4e4e7' }}>
									{expiresInDays > 0 ? `还剩 ${expiresInDays} 天` : '即将过期'}
								</div>
							</div>
						</div>
					</div>
				)}
			</div>
		</div>
	);
};
