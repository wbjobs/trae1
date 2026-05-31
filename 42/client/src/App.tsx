import React, { useState, useEffect } from 'react';
import { Lobby } from './Lobby';
import { MeetingPage } from './MeetingPage';
import { ReplayPage } from './ReplayPage';

type Route = 'lobby' | 'meeting' | 'replay';

interface MeetingContext {
	code: string;
	peerId: string;
	name: string;
	routerRtpCapabilities: mediasoupClient.types.RtpCapabilities;
}

const getInitialRoute = (): { route: Route; recordingId?: string } => {
	const path = window.location.pathname;
	if (path.startsWith('/replay/')) {
		const recordingId = path.replace('/replay/', '');
		if (recordingId) {
			return { route: 'replay', recordingId };
		}
	}
	return { route: 'lobby' };
};

const App: React.FC = () => {
	const initial = getInitialRoute();
	const [route, setRoute] = useState<Route>(initial.route);
	const [recordingId, setRecordingId] = useState<string>(initial.recordingId || '');
	const [meetingCtx, setMeetingCtx] = useState<MeetingContext | null>(null);

	useEffect(() => {
		const handlePopState = () => {
			const path = window.location.pathname;
			if (path.startsWith('/replay/')) {
				setRecordingId(path.replace('/replay/', ''));
				setRoute('replay');
			} else {
				setRoute('lobby');
			}
		};
		window.addEventListener('popstate', handlePopState);
		return () => window.removeEventListener('popstate', handlePopState);
	}, []);

	const handleJoinMeeting = (ctx: MeetingContext) => {
		setMeetingCtx(ctx);
		setRoute('meeting');
	};

	const handleLeaveMeeting = () => {
		setMeetingCtx(null);
		setRoute('lobby');
	};

	const handleBackFromReplay = () => {
		window.history.pushState({}, '', '/');
		setRoute('lobby');
	};

	if (route === 'replay' && recordingId) {
		return <ReplayPage recordingId={recordingId} onBack={handleBackFromReplay} />;
	}

	if (route === 'meeting' && meetingCtx) {
		return (
			<MeetingPage
				code={meetingCtx.code}
				peerId={meetingCtx.peerId}
				name={meetingCtx.name}
				routerRtpCapabilities={meetingCtx.routerRtpCapabilities}
				onLeave={handleLeaveMeeting}
			/>
		);
	}

	return <Lobby onJoinMeeting={handleJoinMeeting} />;
};

export default App;
