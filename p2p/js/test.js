const { BridgeP2PClient } = require('./index');
const net = require('net');

async function testJsP2P() {
  console.log('Testing JS BridgeP2PClient...');
  const server = net.createServer((sock) => {
    sock.on('data', (buf) => {
      if (buf.subarray(0, 4).toString() === 'BRG1') {
        const len = buf.readUInt32BE(4);
        const payload = JSON.parse(buf.subarray(8, 8 + len).toString());
        if (payload.type === 'rpc_req') {
          const res = JSON.stringify({
            type: 'rpc_res',
            id: payload.id,
            result: 'js_ok'
          });
          const resBuf = Buffer.from(res);
          const header = Buffer.alloc(8);
          Buffer.from('BRG1').copy(header, 0, 0, 4);
          header.writeUInt32BE(resBuf.length, 4);
          sock.write(Buffer.concat([header, resBuf]));
        }
      }
    });
  });

  await new Promise((r) => server.listen(19994, '127.0.0.1', r));

  const client = new BridgeP2PClient('tcp://127.0.0.1:19994');
  await client.connect();

  const res = await client.call('ping', {});
  if (res !== 'js_ok') {
    throw new Error(`Unexpected result: ${res}`);
  }
  console.log('JS test passed:', res);

  client.close();
  server.close();
}

testJsP2P().catch((err) => {
  console.error(err);
  process.exit(1);
});
