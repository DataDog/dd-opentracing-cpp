// This is an HTTP server that listens on port 8126, and prints to standard
// output a JSON representation of all traces that it receives.

const http = require('http');
const msgpack = require('massagepack');
const process = require('process');

function handleTraceSegments(segments) {
    console.log(msgpack.encodeJSON(segments));
}

const requestListener = function (request, response) {
  if (!request.url.endsWith('/traces')) {
    // The agent also supports telemetry endpoints.
    // We serve the [...]/traces endpoints only.
    response.writeHead(404);
    response.end();
    return;
  }

  let body = [];
  request.on('data', chunk => {
    body.push(chunk);
  }).on('end', () => {
    body = Buffer.concat(body);
    const trace_segments = msgpack.decode(body);
    handleTraceSegments(trace_segments);
    response.writeHead(200);
    response.end(JSON.stringify({}));
  });
};

const server = http.createServer(requestListener);
const port = 8126;
server.listen(port, () => console.log(`node.js web server (agent) is running on port ${port}`));

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.close(() => process.exit(0));
});
