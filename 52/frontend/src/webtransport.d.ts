interface WebTransportDatagramDuplexStream {
  readable: ReadableStream<Uint8Array>;
  writable: WritableStream<Uint8Array>;
  maxDatagramSize: number;
}

interface WebTransportOptions {
  serverCertificateHashes?: Array<{
    algorithm: string;
    value: Uint8Array;
  }>;
  congestionControl?: 'default' | 'throughput' | 'low-latency';
  allowPooling?: boolean;
  requireUnreliable?: boolean;
}

interface WebTransportCloseInfo {
  closeCode: number;
  reason: string;
}

interface WebTransportBidirectionalStream {
  readable: ReadableStream<Uint8Array>;
  writable: WritableStream<Uint8Array>;
}

declare class WebTransport {
  constructor(url: string, options?: WebTransportOptions);
  ready: Promise<undefined>;
  closed: Promise<WebTransportCloseInfo>;
  datagrams: WebTransportDatagramDuplexStream;
  incomingBidirectionalStreams: ReadableStream<WebTransportBidirectionalStream>;
  incomingUnidirectionalStreams: ReadableStream<ReadableStream<Uint8Array>>;
  createBidirectionalStream(): Promise<WebTransportBidirectionalStream>;
  createUnidirectionalStream(): Promise<WritableStream<Uint8Array>>;
  close(info?: WebTransportCloseInfo): void;
}

interface Window {
  WebTransport: typeof WebTransport;
}
