import http from 'http';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { WebSocketServer } from 'ws';
import { spawn } from 'child_process';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const uiDir = path.join(__dirname, '..', 'ui');
const port = 3000;

const defaultEngine = path.join(__dirname, '..', 'engine', 'build', 'blitz_engine.exe');
const releaseEngine = path.join(__dirname, '..', 'engine', 'build', 'Release', 'blitz_engine.exe');
const enginePath = process.env.CHESS_ENGINE_PATH || (fs.existsSync(releaseEngine) ? releaseEngine : defaultEngine);
let engine = null;
let engineReady = false;
let engineQueue = [];

function startEngine() {
  const env = {
    ...process.env,
    PATH: `C:\\msys64\\mingw64\\bin;${process.env.PATH || ''}`
  };
  engine = spawn(enginePath, [], { stdio: ['pipe', 'pipe', 'pipe'], env });
  engine.stdout.setEncoding('utf8');
  engine.stderr.setEncoding('utf8');

  engine.stdout.on('data', (data) => {
    const lines = data.toString().split(/\r?\n/).filter(Boolean);
    lines.forEach((line) => broadcast({ type: 'uci', line }));
  });

  engine.stderr.on('data', (data) => {
    const lines = data.toString().split(/\r?\n/).filter(Boolean);
    lines.forEach((line) => broadcast({ type: 'uci', line: `info string ${line}` }));
  });

  engine.on('error', (err) => {
    broadcast({ type: 'uci', line: `info string engine error: ${err.message}` });
  });

  engine.on('exit', () => {
    engine = null;
    engineReady = false;
    broadcast({ type: 'uci', line: 'info string engine stopped' });
  });

  sendToEngine('uci');
  sendToEngine('isready');
}

function sendToEngine(command) {
  if (!engine) return;
  engine.stdin.write(command + '\n');
}

const server = http.createServer((req, res) => {
  const urlPath = req.url.split('?')[0];
  const url = urlPath === '/' ? '/index.html' : urlPath;
  const filePath = url.startsWith('/pieces-svg')
    ? path.join(__dirname, '..', url)
    : path.join(uiDir, url);
  fs.readFile(filePath, (err, data) => {
    if (err) {
      console.error(`404: ${url} -> ${filePath}`);
      res.writeHead(404);
      res.end('Not found');
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    const contentType = ext === '.html' ? 'text/html'
      : ext === '.css' ? 'text/css'
      : ext === '.js' ? 'text/javascript'
      : ext === '.svg' ? 'image/svg+xml'
      : 'text/plain';
    res.writeHead(200, { 'Content-Type': contentType });
    res.end(data);
  });
});

server.on('error', (err) => {
  console.error('Server error:', err.message);
});

const wss = new WebSocketServer({ server });
wss.on('error', (err) => {
  console.error('WebSocket error:', err.message);
});
const clients = new Set();

function broadcast(message) {
  const data = JSON.stringify(message);
  for (const ws of clients) {
    if (ws.readyState === ws.OPEN) ws.send(data);
  }
}

wss.on('connection', (ws) => {
  clients.add(ws);
  if (!engine) startEngine();

  ws.on('message', (raw) => {
    try {
      const msg = JSON.parse(raw.toString());
      if (msg.type === 'uci' && msg.command) {
        sendToEngine(msg.command);
      }
    } catch (_) {
      // ignore
    }
  });

  ws.on('close', () => {
    clients.delete(ws);
  });
});

server.listen(port, () => {
  console.log(`UI at http://localhost:${port}`);
  if (engineQueue.length > 0 && engine) {
    engineQueue.forEach(sendToEngine);
    engineQueue = [];
  }
});
