const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');
const path = require('path');

const PROTO_PATH = path.join(__dirname, '..', 'proto', 'riskcontrol.proto');
const packageDefinition = protoLoader.loadSync(PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
});
const proto = grpc.loadPackageDefinition(packageDefinition).riskcontrol;

const client = new proto.RiskControlService(
  process.env.GRPC_ADDR || 'localhost:50051',
  grpc.credentials.createInsecure(),
);

async function run() {
  const decisionName = (v) => proto.Decision[v];
  const strategyName = (v) => proto.Strategy[v];

  const scenarios = [
    { user: 'bad-user', action: 'login', desc: 'blacklisted user' },
    { user: 'normal-user', action: 'login', desc: 'normal login' },
    { user: 'busy-user', action: 'post', desc: 'posting user' },
  ];

  console.log('=== Benchmarking ===\n');
  for (const s of scenarios) {
    const latencies = [];
    let pass = 0, reject = 0, verify = 0;
    const N = 100;

    for (let i = 0; i < N; i++) {
      const start = process.hrtime.bigint();
      const res = await new Promise((resolve, reject) => {
        client.Check(
          { user_id: s.user, action_type: s.action, timestamp: Date.now() },
          (err, r) => (err ? reject(err) : resolve(r)),
        );
      });
      const end = process.hrtime.bigint();
      const ms = Number(end - start) / 1e6;
      latencies.push(ms);
      if (res.decision === 0) pass++;
      else if (res.decision === 1) reject++;
      else verify++;
    }

    latencies.sort((a, b) => a - b);
    const p50 = latencies[Math.floor(N * 0.5)];
    const p95 = latencies[Math.floor(N * 0.95)];
    const p99 = latencies[Math.floor(N * 0.99)];
    const avg = latencies.reduce((a, b) => a + b, 0) / N;

    console.log(`Scenario: ${s.desc} (${s.user}/${s.action})`);
    console.log(`  pass=${pass} reject=${reject} verify=${verify}`);
    console.log(`  avg=${avg.toFixed(2)}ms p50=${p50.toFixed(2)}ms p95=${p95.toFixed(2)}ms p99=${p99.toFixed(2)}ms`);
    console.log();
  }

  process.exit(0);
}

run().catch((err) => {
  console.error(err);
  process.exit(1);
});
