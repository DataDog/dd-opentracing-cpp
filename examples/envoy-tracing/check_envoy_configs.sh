#!/bin/bash
required_tools=( docker docker-compose curl )
for i in ${required_tools[@]}; do
	if ! binary=$(type -P "$i"); then
		echo "Missing required tool: $i"
		exit 1
	fi
	if ! [[ -x $binary ]]; then
		echo "Required tool is not executable: $i ( $binary )"
		exit 1
	fi
done

if ! cd "${0%/*}"; then
	echo "failed to change working directory: ${0%/*}"
	exit 1
fi

configs=( envoy-*.yaml )
for i in "${configs[@]}"; do
	x=${i#envoy-}
	x=${x%.yaml}
	versions+=( $x )
done

docker-compose rm -f
docker-compose up -d
for i in "${versions[@]}"; do
	if ! IP=$(docker inspect --format='{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "envoy-tracing_envoy-${i}_1"); then
		echo "failed checking $i: unable to retrieve IP address for envoy-tracing_envoy-${i}_1"
		continue
	fi
	curl -w "\n" "http://$IP/" "http://$IP/healthcheck"
done
sleep 1
echo "check for traces at https://app.datadoghq.com/apm/livetail or https://app.datadoghq.com/apm/traces"
sleep 5
docker-compose kill
