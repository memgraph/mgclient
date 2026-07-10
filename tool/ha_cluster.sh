#!/bin/bash

# Start (or stop) a local Memgraph high-availability cluster in Docker: three
# data instances and three coordinators. It is used both by CI and for running
# the client-side routing tests / examples locally.
#
# High availability is a Memgraph Enterprise feature, so `start` requires:
#   MEMGRAPH_ENTERPRISE_LICENSE, MEMGRAPH_ORGANIZATION_NAME
#
# Every container runs on the Docker "host" network (override with
# MEMGRAPH_HA_NETWORK), so each instance uses a distinct port on localhost and
# the addresses the coordinator advertises (127.0.0.1:<port>) are directly
# reachable -- no address resolver needed.
#
# Usage:
#   tool/ha_cluster.sh start   # start, register the topology, wait to converge
#   tool/ha_cluster.sh stop    # stop and remove the containers
#
# On success `start` prints the coordinator to point clients at, e.g.:
#   MEMGRAPH_HA_COORDINATOR_HOST=127.0.0.1 MEMGRAPH_HA_COORDINATOR_PORT=7691

set -Eeuo pipefail

NETWORK="${MEMGRAPH_HA_NETWORK:-host}"
IMAGE="${MEMGRAPH_HA_IMAGE:-memgraph/memgraph}"
COORDINATOR_HOST=127.0.0.1
COORDINATOR_PORT=7691

CONTAINERS=(mg-data1 mg-data2 mg-data3 mg-coord1 mg-coord2 mg-coord3)

# Run mgconsole (bundled in the memgraph image) against the bootstrap
# coordinator, reading queries from stdin.
mgconsole() {
  docker exec -i mg-coord1 mgconsole --host "$COORDINATOR_HOST" \
    --port "$COORDINATOR_PORT"
}

stop_cluster() {
  for name in "${CONTAINERS[@]}"; do
    docker stop "$name" >/dev/null 2>&1 || true
  done
}

start_cluster() {
  if [ -z "${MEMGRAPH_ENTERPRISE_LICENSE:-}" ] ||
     [ -z "${MEMGRAPH_ORGANIZATION_NAME:-}" ]; then
    echo "error: set MEMGRAPH_ENTERPRISE_LICENSE and MEMGRAPH_ORGANIZATION_NAME" \
         "(high availability is a Memgraph Enterprise feature)" >&2
    exit 1
  fi

  local lic=(-e "MEMGRAPH_ENTERPRISE_LICENSE=$MEMGRAPH_ENTERPRISE_LICENSE" \
             -e "MEMGRAPH_ORGANIZATION_NAME=$MEMGRAPH_ORGANIZATION_NAME")

  # Three data instances. On host networking every container shares localhost,
  # so each needs a distinct set of ports.
  docker run -d --rm --name mg-data1 --network "$NETWORK" "${lic[@]}" "$IMAGE" \
    --bolt-port=7688 --management-port=13011 --telemetry-enabled=false
  docker run -d --rm --name mg-data2 --network "$NETWORK" "${lic[@]}" "$IMAGE" \
    --bolt-port=7689 --management-port=13012 --telemetry-enabled=false
  docker run -d --rm --name mg-data3 --network "$NETWORK" "${lic[@]}" "$IMAGE" \
    --bolt-port=7690 --management-port=13013 --telemetry-enabled=false

  # Three coordinators for a Raft quorum. mg-coord1 is the bootstrap leader;
  # the others are added during registration below.
  docker run -d --rm --name mg-coord1 --network "$NETWORK" "${lic[@]}" "$IMAGE" \
    --bolt-port=7691 --coordinator-id=1 --coordinator-port=12121 \
    --coordinator-hostname=127.0.0.1 --management-port=13021 --telemetry-enabled=false
  docker run -d --rm --name mg-coord2 --network "$NETWORK" "${lic[@]}" "$IMAGE" \
    --bolt-port=7692 --coordinator-id=2 --coordinator-port=12122 \
    --coordinator-hostname=127.0.0.1 --management-port=13022 --telemetry-enabled=false
  docker run -d --rm --name mg-coord3 --network "$NETWORK" "${lic[@]}" "$IMAGE" \
    --bolt-port=7693 --coordinator-id=3 --coordinator-port=12123 \
    --coordinator-hostname=127.0.0.1 --management-port=13023 --telemetry-enabled=false

  # Fail early (with logs) if any container didn't come up, rather than as an
  # opaque connection error later.
  sleep 10
  for name in "${CONTAINERS[@]}"; do
    if [ "$(docker inspect -f '{{.State.Running}}' "$name" 2>/dev/null)" != "true" ]; then
      echo "error: Memgraph HA container $name is not running" >&2
      docker logs "$name" || true
      exit 1
    fi
  done

  # Register the topology on the bootstrap coordinator. Addresses are
  # 127.0.0.1:<port> because every container is on host networking.
  printf '%s\n' \
    'ADD COORDINATOR 2 WITH CONFIG {"bolt_server": "127.0.0.1:7692", "coordinator_server": "127.0.0.1:12122", "management_server": "127.0.0.1:13022"};' \
    'ADD COORDINATOR 3 WITH CONFIG {"bolt_server": "127.0.0.1:7693", "coordinator_server": "127.0.0.1:12123", "management_server": "127.0.0.1:13023"};' \
    'REGISTER INSTANCE instance_1 WITH CONFIG {"bolt_server": "127.0.0.1:7688", "management_server": "127.0.0.1:13011", "replication_server": "127.0.0.1:10001"};' \
    'REGISTER INSTANCE instance_2 WITH CONFIG {"bolt_server": "127.0.0.1:7689", "management_server": "127.0.0.1:13012", "replication_server": "127.0.0.1:10002"};' \
    'REGISTER INSTANCE instance_3 WITH CONFIG {"bolt_server": "127.0.0.1:7690", "management_server": "127.0.0.1:13013", "replication_server": "127.0.0.1:10003"};' \
    'SET INSTANCE instance_1 TO MAIN;' \
    | mgconsole

  # Wait for the cluster to converge: the coordinator must advertise a main
  # (WRITE) and at least one replica.
  local converged=false instances=""
  for _ in $(seq 1 60); do
    instances="$(echo 'SHOW INSTANCES;' | mgconsole 2>/dev/null || true)"
    if echo "$instances" | grep -qiw main &&
       echo "$instances" | grep -qiw replica; then
      converged=true
      break
    fi
    sleep 1
  done
  if [ "$converged" != "true" ]; then
    echo "error: HA cluster did not converge" >&2
    echo "$instances" >&2
    exit 1
  fi

  echo "HA cluster ready. Point clients at the coordinator:"
  echo "  MEMGRAPH_HA_COORDINATOR_HOST=$COORDINATOR_HOST" \
       "MEMGRAPH_HA_COORDINATOR_PORT=$COORDINATOR_PORT"
}

case "${1:-start}" in
  start) start_cluster ;;
  stop) stop_cluster ;;
  *)
    echo "usage: $0 [start|stop]" >&2
    exit 2
    ;;
esac
