#!/bin/bash

# Start (or stop) a local Memgraph high-availability cluster in Docker: three
# data instances and three coordinators. It is used both by CI and for running
# the client-side routing tests / examples locally.
#
# High availability is a Memgraph Enterprise feature, so `start` requires:
#   MEMGRAPH_ENTERPRISE_LICENSE, MEMGRAPH_ORGANIZATION_NAME
#
# The containers run on the Docker network named by MEMGRAPH_HA_NETWORK
# (default "host"), which also selects the addressing scheme:
#
#   * "host": every container shares localhost, so each instance gets a
#     distinct port and the advertised addresses are 127.0.0.1:<port>.
#     Reachable from anything on the host. This is what the mgclient tests use.
#
#   * a user-defined bridge network (any other value): each container gets its
#     own IP, so they share the standard ports and the advertised addresses are
#     the container names (e.g. mg-data1:7687). Reachable from another container
#     on the same network -- the model a containerized test runner (e.g.
#     pymgclient's) uses.
#
# Either way the advertised addresses are directly reachable by the intended
# client, so no address resolver is needed.
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

DATA_NAMES=(mg-data1 mg-data2 mg-data3)
COORD_NAMES=(mg-coord1 mg-coord2 mg-coord3)

# Per-instance ports and advertised address hosts, indexed 0..2. The two
# networking modes differ only in these tables.
if [ "$NETWORK" = "host" ]; then
  DATA_HOST=(127.0.0.1 127.0.0.1 127.0.0.1)
  COORD_HOST=(127.0.0.1 127.0.0.1 127.0.0.1)
  DATA_BOLT=(7688 7689 7690)
  COORD_BOLT=(7691 7692 7693)
  DATA_MGMT=(13011 13012 13013)
  COORD_MGMT=(13021 13022 13023)
  COORD_CPORT=(12121 12122 12123)
  DATA_REPL=(10001 10002 10003)
else
  DATA_HOST=("${DATA_NAMES[@]}")
  COORD_HOST=("${COORD_NAMES[@]}")
  DATA_BOLT=(7687 7687 7687)
  COORD_BOLT=(7687 7687 7687)
  DATA_MGMT=(13011 13011 13011)
  COORD_MGMT=(13011 13011 13011)
  COORD_CPORT=(12121 12121 12121)
  DATA_REPL=(10000 10000 10000)
fi

# Run mgconsole (bundled in the memgraph image) against the bootstrap
# coordinator, reading queries from stdin. Executed inside mg-coord1, so
# localhost + coord1's bolt port reaches its own server in either mode.
mgconsole() {
  docker exec -i mg-coord1 mgconsole --host localhost --port "${COORD_BOLT[0]}"
}

stop_cluster() {
  for name in "${DATA_NAMES[@]}" "${COORD_NAMES[@]}"; do
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

  if [ "$NETWORK" != "host" ]; then
    docker network create "$NETWORK" >/dev/null 2>&1 || true
  fi

  local lic=(-e "MEMGRAPH_ENTERPRISE_LICENSE=$MEMGRAPH_ENTERPRISE_LICENSE" \
             -e "MEMGRAPH_ORGANIZATION_NAME=$MEMGRAPH_ORGANIZATION_NAME")

  # Three data instances.
  local i
  for i in 0 1 2; do
    docker run -d --rm --name "${DATA_NAMES[$i]}" --network "$NETWORK" "${lic[@]}" \
      "$IMAGE" --bolt-port="${DATA_BOLT[$i]}" \
      --management-port="${DATA_MGMT[$i]}" --telemetry-enabled=false
  done

  # Three coordinators for a Raft quorum. mg-coord1 is the bootstrap leader;
  # the others are added during registration below.
  for i in 0 1 2; do
    docker run -d --rm --name "${COORD_NAMES[$i]}" --network "$NETWORK" "${lic[@]}" \
      "$IMAGE" --bolt-port="${COORD_BOLT[$i]}" --coordinator-id="$((i + 1))" \
      --coordinator-port="${COORD_CPORT[$i]}" \
      --coordinator-hostname="${COORD_HOST[$i]}" \
      --management-port="${COORD_MGMT[$i]}" --telemetry-enabled=false
  done

  # Fail early (with logs) if any container didn't come up, rather than as an
  # opaque connection error later.
  sleep 10
  for name in "${DATA_NAMES[@]}" "${COORD_NAMES[@]}"; do
    if [ "$(docker inspect -f '{{.State.Running}}' "$name" 2>/dev/null)" != "true" ]; then
      echo "error: Memgraph HA container $name is not running" >&2
      docker logs "$name" || true
      exit 1
    fi
  done

  # Register the topology on the bootstrap coordinator. Addresses use the mode's
  # advertised host + port so the coordinator (and, in turn, clients) can reach
  # every instance.
  {
    for i in 1 2; do
      printf 'ADD COORDINATOR %d WITH CONFIG {"bolt_server": "%s:%s", "coordinator_server": "%s:%s", "management_server": "%s:%s"};\n' \
        "$((i + 1))" \
        "${COORD_HOST[$i]}" "${COORD_BOLT[$i]}" \
        "${COORD_HOST[$i]}" "${COORD_CPORT[$i]}" \
        "${COORD_HOST[$i]}" "${COORD_MGMT[$i]}"
    done
    for i in 0 1 2; do
      printf 'REGISTER INSTANCE instance_%d WITH CONFIG {"bolt_server": "%s:%s", "management_server": "%s:%s", "replication_server": "%s:%s"};\n' \
        "$((i + 1))" \
        "${DATA_HOST[$i]}" "${DATA_BOLT[$i]}" \
        "${DATA_HOST[$i]}" "${DATA_MGMT[$i]}" \
        "${DATA_HOST[$i]}" "${DATA_REPL[$i]}"
    done
    printf 'SET INSTANCE instance_1 TO MAIN;\n'
  } | mgconsole

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
  echo "  MEMGRAPH_HA_COORDINATOR_HOST=${COORD_HOST[0]}" \
       "MEMGRAPH_HA_COORDINATOR_PORT=${COORD_BOLT[0]}"
}

case "${1:-start}" in
  start) start_cluster ;;
  stop) stop_cluster ;;
  *)
    echo "usage: $0 [start|stop]" >&2
    exit 2
    ;;
esac
