const net = require('net');
const url = require('url');

const MAGIC = Buffer.from('BRG1');
const MAX_PAYLOAD = 16 * 1024 * 1024;

class BridgeP2PClient {
  constructor(uriStr) {
    this.uriStr = uriStr;
    const parsed = url.parse(uriStr);
    this.proto = parsed.protocol ? parsed.protocol.replace(':', '') : 'tcp';
    this.host = parsed.hostname || '127.0.0.1';
    this.port = parseInt(parsed.port || '9001', 10);
    this.path = parsed.path;

    this.socket = null;
    this.buffer = Buffer.alloc(0);
    this.reqSeq = 0;
    this.pending = new Map();
    this.eventHandlers = new Map();
    this.connected = false;
  }

  connect() {
    return new Promise((resolve, reject) => {
      const opts = this.proto === 'unix' ? { path: this.path } : { host: this.host, port: this.port };
      this.socket = net.createConnection(opts, () => {
        this.connected = true;
        this.socket.setNoDelay(true);
        resolve(this);
      });

      this.socket.on('error', (err) => {
        if (!this.connected) reject(err);
      });

      this.socket.on('data', (chunk) => {
        this._onData(chunk);
      });

      this.socket.on('close', () => {
        this.connected = false;
        for (const [id, p] of this.pending.entries()) {
          p.reject(new Error('Connection closed'));
        }
        this.pending.clear();
      });
    });
  }

  _onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (this.buffer.length >= 8) {
      if (this.buffer.subarray(0, 4).toString() !== 'BRG1') {
        this.socket.destroy();
        throw new Error('Invalid magic');
      }
      const len = this.buffer.readUInt32BE(4);
      if (len > MAX_PAYLOAD) {
        this.socket.destroy();
        throw new Error('Payload too large');
      }
      if (this.buffer.length < 8 + len) {
        break; // Wait for more data
      }
      const payloadBuf = this.buffer.subarray(8, 8 + len);
      this.buffer = this.buffer.subarray(8 + len);

      try {
        const msg = JSON.parse(payloadBuf.toString('utf8'));
        this._handleMessage(msg);
      } catch (err) {
        console.error('Failed to parse frame:', err);
      }
    }
  }

  _handleMessage(msg) {
    if (msg.type === 'rpc_res') {
      const p = this.pending.get(msg.id);
      if (p) {
        this.pending.delete(msg.id);
        if (msg.error) {
          p.reject(new Error(`RPC Error: ${JSON.stringify(msg.error)}`));
        } else {
          p.resolve(msg.result);
        }
      }
    } else if (msg.type === 'event') {
      const h = this.eventHandlers.get(msg.topic) || this.eventHandlers.get('*');
      if (h) {
        h(msg.topic, msg.data);
      }
    }
  }

  _sendFrame(obj) {
    const jsonBuf = Buffer.from(JSON.stringify(obj), 'utf8');
    const header = Buffer.alloc(8);
    MAGIC.copy(header, 0, 0, 4);
    header.writeUInt32BE(jsonBuf.length, 4);
    this.socket.write(Buffer.concat([header, jsonBuf]));
  }

  call(method, params, timeoutMs = 5000) {
    return new Promise((resolve, reject) => {
      this.reqSeq++;
      const id = `js-${this.reqSeq}`;
      const timer = setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          reject(new Error(`RPC timeout after ${timeoutMs}ms`));
        }
      }, timeoutMs);

      this.pending.set(id, {
        resolve: (res) => { clearTimeout(timer); resolve(res); },
        reject: (err) => { clearTimeout(timer); reject(err); }
      });

      this._sendFrame({
        type: 'rpc_req',
        id,
        method,
        params
      });
    });
  }

  publish(topic, data) {
    this._sendFrame({
      type: 'event',
      topic,
      data
    });
  }

  subscribe(topic, handler) {
    this.eventHandlers.set(topic, handler);
  }

  close() {
    if (this.socket) {
      this.socket.end();
      this.socket.destroy();
    }
  }
}

module.exports = { BridgeP2PClient };
