# Envoy Tracing Example

This example uses Envoy's dynamic opentracing feature to load the DataDog opentracing module and use it to trace requests
between a front-end service, and simple backends.

# Credits

This is based on the (front-proxy)[https://github.com/envoyproxy/envoy/tree/master/examples/front-proxy] example from the 
(Envoy)[https://github.com/envoyproxy/envoy] project.

# Launch

```bash
docker-compose up
```

This will build and launch
- front-envoy
- service1
- service2

# Interact

To interact with these services, a docker container needs to be connected on the network created by docker-compose.

```sh-session
$ docker run --net=envoy-tracing_envoymesh --rm -i -t busybox
/ # wget -q -O- http://front-envoy/service/1
Hello from behind Envoy (service 1)! hostname: 6060fcea4f2a resolvedhostname: 192.168.16.4
/ # wget -q -O- http://front-envoy/service/2
Hello from behind Envoy (service 2)! hostname: 4d908665f937 resolvedhostname: 192.168.16.5
/ # wget -q -O- http://service1/trace/1
Hello from behind Envoy (service 1)! hostname: 6060fcea4f2a resolvedhostname: 192.168.16.4
/ # 
```
