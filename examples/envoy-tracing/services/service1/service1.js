// This is an HTTP server that listens on port 8080 and responds to all
// requests with some text, including the request headers as JSON.
//
// The response status defaults to 200, but can be changed by the caller
// by setting the resource to "[...]/status/<code>", e.g. "/status/500" to
// receive response status 500.

const http = require('http');
const process = require('process');

const requestListener = function (request, response) {
  const responseBody = JSON.stringify({
    "service": "service1",
    "headers": request.headers
  }, null, 2);
  console.log(responseBody);

  // "[...]/status/<code>" makes us respond with status <code>.
  let status = 200;
  const match = request.url.match(/.*\/status\/([0-9]+)$/);
  if (match  !== null) {
    const [full, statusString] = match;
    status = Number.parseInt(statusString, 10);
  }
  response.writeHead(status);
  response.end(responseBody);
}

const server = http.createServer(requestListener);
const port = 80;
server.listen(port, () => console.log(`http node.js web server (service1) is running on port ${port}`));

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.close(() => process.exit(0));
});
