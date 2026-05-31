import React, { useState, useRef, useEffect, useCallback } from 'react';

interface RecorderPlayerProps {
	src: string;
	poster?: string;
}

const SPEED_OPTIONS = [0.5, 1, 1.5, 2];

export const RecorderPlayer: React.FC<RecorderPlayerProps> = ({ src, poster }) => {
	const videoRef = useRef<HTMLVideoElement>(null);
	const [isPlaying, setIsPlaying] = useState(false);
	const [currentTime, setCurrentTime] = useState(0);
	const [duration, setDuration] = useState(0);
	const [playbackRate, setPlaybackRate] = useState(1);
	const [volume, setVolume] = useState(1);
	const [muted, setMuted] = useState(false);
	const [showSpeedMenu, setShowSpeedMenu] = useState(false);
	const [isLoading, setIsLoading] = useState(true);
	const [error, setError] = useState('');

	useEffect(() => {
		const video = videoRef.current;
		if (!video) return;

		const onLoadedMetadata = () => {
			setDuration(video.duration || 0);
			setIsLoading(false);
		};

		const onTimeUpdate = () => {
			setCurrentTime(video.currentTime);
		};

		const onEnded = () => {
			setIsPlaying(false);
		};

		const onError = () => {
			setError('视频加载失败');
			setIsLoading(false);
		};

		video.addEventListener('loadedmetadata', onLoadedMetadata);
		video.addEventListener('timeupdate', onTimeUpdate);
		video.addEventListener('ended', onEnded);
		video.addEventListener('error', onError);

		return () => {
			video.removeEventListener('loadedmetadata', onLoadedMetadata);
			video.removeEventListener('timeupdate', onTimeUpdate);
			video.removeEventListener('ended', onEnded);
			video.removeEventListener('error', onError);
		};
	}, [src]);

	const formatTime = (seconds: number): string => {
		if (!isFinite(seconds)) return '0:00';
		const m = Math.floor(seconds / 60);
		const s = Math.floor(seconds % 60);
		return `${m}:${s.toString().padStart(2, '0')}`;
	};

	const togglePlay = useCallback(() => {
		const video = videoRef.current;
		if (!video) return;
		if (video.paused) {
			video.play();
			setIsPlaying(true);
		} else {
			video.pause();
			setIsPlaying(false);
		}
	}, []);

	const handleSeek = (e: React.ChangeEvent<HTMLInputElement>) => {
		const video = videoRef.current;
		if (!video) return;
		const time = parseFloat(e.target.value);
		video.currentTime = time;
		setCurrentTime(time);
	};

	const handleSpeedChange = (rate: number) => {
		const video = videoRef.current;
		if (!video) return;
		video.playbackRate = rate;
		setPlaybackRate(rate);
		setShowSpeedMenu(false);
	};

	const handleVolumeChange = (e: React.ChangeEvent<HTMLInputElement>) => {
		const video = videoRef.current;
		if (!video) return;
		const v = parseFloat(e.target.value);
		video.volume = v;
		setVolume(v);
		if (v > 0 && muted) {
			video.muted = false;
			setMuted(false);
		}
	};

	const toggleMute = () => {
		const video = videoRef.current;
		if (!video) return;
		video.muted = !muted;
		setMuted(!muted);
	};

	const progressPercentage = duration > 0 ? (currentTime / duration) * 100 : 0;

	return (
		<div style={{
			background: '#000',
			borderRadius: 12,
			overflow: 'hidden',
			position: 'relative',
			width: '100%',
			maxWidth: 960,
			margin: '0 auto'
		}}>
			<div style={{ position: 'relative' }}>
				<video
					ref={videoRef}
					src={src}
					poster={poster}
					style={{
						width: '100%',
						display: 'block',
						background: '#000',
						aspectRatio: '16/9'
					}}
					onClick={togglePlay}
				/>
				{isLoading && !error && (
					<div style={{
						position: 'absolute',
						top: 0,
						left: 0,
						right: 0,
						bottom: 0,
						display: 'flex',
						alignItems: 'center',
						justifyContent: 'center',
						background: 'rgba(0,0,0,0.5)'
					}}>
						<div style={{
							width: 40,
							height: 40,
							border: '3px solid #3f3f52',
							borderTopColor: '#818cf8',
							borderRadius: '50%',
							animation: 'spin 0.8s linear infinite'
						}} />
					</div>
				)}
				{error && (
					<div style={{
						position: 'absolute',
						top: 0,
						left: 0,
						right: 0,
						bottom: 0,
						display: 'flex',
						alignItems: 'center',
						justifyContent: 'center',
						background: 'rgba(0,0,0,0.7)',
						color: '#ef4444',
						fontSize: 16
					}}>
						{error}
					</div>
				)}
			</div>

			<div style={{
				padding: '12px 16px',
				background: '#1e1e2a',
				display: 'flex',
				alignItems: 'center',
				gap: 12
			}}>
				<button
					onClick={togglePlay}
					style={{
						background: 'none',
						border: 'none',
						color: '#e4e4e7',
						cursor: 'pointer',
						fontSize: 20,
						padding: 4,
						width: 36,
						height: 36,
						borderRadius: '50%',
						display: 'flex',
						alignItems: 'center',
						justifyContent: 'center'
					}}
				>
					{isPlaying ? '⏸' : '▶'}
				</button>

				<span style={{ fontSize: 13, color: '#a1a1aa', minWidth: 45, textAlign: 'right' }}>
					{formatTime(currentTime)}
				</span>

				<input
					type="range"
					min={0}
					max={duration || 100}
					value={currentTime}
					onChange={handleSeek}
					style={{
						flex: 1,
						height: 4,
						WebkitAppearance: 'none',
						appearance: 'none',
						background: `linear-gradient(to right, #818cf8 ${progressPercentage}%, #3f3f52 ${progressPercentage}%)`,
						borderRadius: 2,
						outline: 'none',
						cursor: 'pointer',
						accentColor: '#818cf8'
					}}
				/>

				<span style={{ fontSize: 13, color: '#a1a1aa', minWidth: 45 }}>
					{formatTime(duration)}
				</span>

				<button
					onClick={toggleMute}
					style={{
						background: 'none',
						border: 'none',
						color: '#e4e4e7',
						cursor: 'pointer',
						fontSize: 16,
						padding: 4
					}}
				>
					{muted || volume === 0 ? '🔇' : volume < 0.5 ? '🔉' : '🔊'}
				</button>

				<input
					type="range"
					min={0}
					max={1}
					step={0.1}
					value={muted ? 0 : volume}
					onChange={handleVolumeChange}
					style={{
						width: 60,
						height: 4,
						WebkitAppearance: 'none',
						appearance: 'none',
						background: '#3f3f52',
						borderRadius: 2,
						outline: 'none',
						cursor: 'pointer',
						accentColor: '#818cf8'
					}}
				/>

				<div style={{ position: 'relative' }}>
					<button
						onClick={() => setShowSpeedMenu(!showSpeedMenu)}
						style={{
							background: '#3f3f52',
							border: 'none',
							color: '#e4e4e7',
							cursor: 'pointer',
							fontSize: 13,
							padding: '6px 12px',
							borderRadius: 6,
							minWidth: 50
						}}
					>
						{playbackRate}x
					</button>
					{showSpeedMenu && (
						<div style={{
							position: 'absolute',
							bottom: '100%',
							right: 0,
							marginBottom: 8,
							background: '#27273a',
							borderRadius: 8,
							boxShadow: '0 4px 12px rgba(0,0,0,0.3)',
							overflow: 'hidden',
							zIndex: 10
						}}>
							{SPEED_OPTIONS.map((rate) => (
								<button
									key={rate}
									onClick={() => handleSpeedChange(rate)}
									style={{
										display: 'block',
										width: '100%',
										padding: '10px 20px',
										background: rate === playbackRate ? '#818cf8' : 'transparent',
										border: 'none',
										color: '#e4e4e7',
										cursor: 'pointer',
										fontSize: 13,
										textAlign: 'left'
									}}
								>
									{rate}x
								</button>
							))}
						</div>
					)}
				</div>
			</div>
		</div>
	);
};
