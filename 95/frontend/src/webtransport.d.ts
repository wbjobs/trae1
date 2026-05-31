/// <reference types="vite/client" />

interface WebTransportCloseInfo {
  closeCode?: number;
  reason?: string;
}

interface WebTransportDatagramDuplexStream {
  readonly readable: ReadableStream<Uint8Array>;
  readonly writable: WritableStream<Uint8Array>;
  readonly maxDatagramSize: number;
}

interface WebTransportHash {
  algorithm: string;
  value: BufferSource;
}

interface WebTransportOptions {
  allowPooling?: boolean;
  requireUnreliable?: boolean;
  serverCertificateHashes?: WebTransportHash[];
  congestionControl?: 'default' | 'throughput' | 'low-latency';
}

interface WebTransportConnectionStats {
  timestamp: DOMHighResTimeStamp;
  bytesSent: number;
  bytesReceived: number;
}

type WebTransportReliabilityMode = 'pending' | 'reliable-only' | 'supports-unreliable';
type WebTransportCongestionControl = 'default' | 'throughput' | 'low-latency';

declare class WebTransport {
  constructor(url: string | URL, options?: WebTransportOptions);
  readonly ready: Promise<undefined>;
  readonly closed: Promise<WebTransportCloseInfo>;
  readonly draining: Promise<undefined>;
  readonly reliability: WebTransportReliabilityMode;
  readonly congestionControl: WebTransportCongestionControl;
  readonly datagrams: WebTransportDatagramDuplexStream;
  close(closeInfo?: WebTransportCloseInfo): void;
  getStats(): Promise<WebTransportConnectionStats>;
}

interface Window {
  WebTransport: typeof WebTransport;
}
