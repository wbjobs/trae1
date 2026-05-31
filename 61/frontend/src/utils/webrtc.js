export const RTC_CONFIG = {
  iceServers: [
    { urls: 'stun:stun.l.google.com:19302' },
    { urls: 'stun:stun1.l.google.com:19302' },
  ],
};

export function createPeerConnection() {
  const pc = new RTCPeerConnection(RTC_CONFIG);
  return pc;
}

export function createDataChannel(pc, label = 'mesh', opts = {}) {
  return pc.createDataChannel(label, {
    ordered: false,
    maxRetransmits: 0,
    ...opts,
  });
}

export function sendDC(dc, obj) {
  if (!dc || dc.readyState !== 'open') return false;
  try {
    dc.send(JSON.stringify(obj));
    return true;
  } catch (e) {
    console.warn('dc send failed', e);
    return false;
  }
}
