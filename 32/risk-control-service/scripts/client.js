const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');

const PROTO_PATH = path.join(__dirname, '..', 'proto', 'riskcontrol.proto');

const packageDefinition = protoLoader.loadSync(PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});

const protoDescriptor = grpc.loadPackageDefinition(packageDefinition);
const { RiskControlService } = protoDescriptor.riskcontrol;

const client = new RiskControlService(
  process.env.GRPC_ADDR || 'localhost:50051',
  grpc.credentials.createInsecure(),
);

const decisionName = (v) => protoDescriptor.riskcontrol.Decision[v] || 'UNKNOWN';
const strategyName = (v) => protoDescriptor.riskcontrol.Strategy[v] || 'UNKNOWN';

function check(user_id, action_type) {
  return new Promise((resolve, reject) => {
    client.Check(
      { user_id, action_type, timestamp: Date.now() },
      (err, resp) => {
        if (err) return reject(err);
        resolve({
          ...resp,
          decision: decisionName(resp.decision),
          hit_strategy: strategyName(resp.hit_strategy),
        });
      },
    );
  });
}

async function main() {
  const userId = process.argv[2] || 'user-123';
  const action = process.argv[3] || 'login';
  const n = parseInt(process.argv[4] || '1', 10);

  for (let i = 0; i < n; i++) {
    const res = await check(userId, action);
    console.log(`[${i + 1}/${n}]`, res);
  }
  process.exit(0);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
