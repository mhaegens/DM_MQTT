import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import * as aedesModule from 'aedes';
import {WebSocketServer,createWebSocketStream} from 'ws';
const broker = typeof aedesModule.Aedes?.createBroker === 'function' ? await aedesModule.Aedes.createBroker() : aedesModule.default();
const root = path.resolve(process.env.DM_HTML_ROOT || fileURLToPath(new URL('../dashboard/html/',import.meta.url)));

const server = createServer(async (req,res) => {
  try {
    let url = decodeURIComponent(new URL(req.url,'http://localhost').pathname);
    if(url === '/') {res.writeHead(302,{Location:'/dashboard/'});res.end();return;}
    if(url.endsWith('/')) url += 'index.html';
    const file = path.resolve(root,'.'+url);
    if(!file.startsWith(root+path.sep)) throw Error('path');
    let data;
    if (url === '/assets/site-config.js') {
      try { data = await readFile(path.join(root,'assets/site-config.local.js')); }
      catch (error) { if (error.code !== 'ENOENT') throw error; data = await readFile(file); }
    } else data = await readFile(file);
    res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml'})[path.extname(file)]||'application/octet-stream');
    res.end(data);
  } catch (_) {res.writeHead(404);res.end('Not found');}
});
const ws = new WebSocketServer({server,path:'/mqtt'});
ws.on('connection',sock=>broker.handle(createWebSocketStream(sock)));
const port = Number(process.env.DM_QA_PORT || 8787);
server.listen(port,'127.0.0.1',()=>console.log(`QA server ready at http://127.0.0.1:${port}/dashboard/`));
