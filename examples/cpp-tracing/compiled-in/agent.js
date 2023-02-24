// This is an HTTP server that listens on port 8126, and prints to standard
// output a JSON representation of all traces that it receives.

const http = require('http');
const mpack = require('massagepack');
const process = require('process');

function handleTraceSegments(segments) {
    console.log(mpack.encodeJSON(segments));
}

const requestListener = function (request, response) {
  let body = [];
  request.on('data', chunk => {
    // console.log('Received a chunk of data.');
    body.push(chunk);
  }).on('end', () => {
    // console.log('Received end of request.');
    body = Buffer.concat(body);
    const trace_segments = mpack.decode(body);
    handleTraceSegments(trace_segments);
    response.writeHead(200);
    response.end(JSON.stringify({}));
  });
};

const port = 8126;
const server = http.createServer(requestListener);
server.listen(port).on('listening', () =>
    console.log(`node.js web server (agent) is running on port ${port}`));

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.close(() => process.exit(0));
});

process.on('SIGINT', function () {
  console.log('Received SIGINT');
  server.close(() => process.exit(0));
});
