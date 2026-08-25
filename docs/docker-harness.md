# Docker multi-node harness

Runs 4 isolated `forgechain` node containers on their own Docker network, so
you can observe propagation, sync, and (later) fork resolution across real,
separate processes -- not multiple `Node` objects sharing one process
(`PropagationTest.cpp`), and not two terminals sharing one host's loopback
interface.

## Topology

```
node1 (mines every 10s)
  ^
  | node2 connects to node1
node2
  ^
  | node3 connects to node2
node3
  ^
  | node4 connects to node3
node4
```

All four nodes mine (at different intervals: 10s/2s/5s/10s) and each has its
own `--reward-address`, so each accumulates its own coinbase rewards
independently. Blocks and transactions propagate outward through the chain
via the same INV/GETDATA/BLOCK relay logic exercised in
`PropagationTest.cpp`.

Nodes connect to each other using **fixed IP addresses** (`172.28.0.11`
through `172.28.0.14`), not Docker's service-name DNS. This is because
`connect_to` (`src/network/TcpSocket.cpp`) uses `inet_pton`, which only
accepts literal IP addresses -- it does not resolve hostnames. If you add
more nodes, assign them the next free IP in the `172.28.0.0/24` subnet and
update `docker-compose.yml`'s `ipv4_address` fields accordingly.

### Reward addresses

Each node's `--reward-address` in `docker-compose.yml` is a real address
generated with `wallet` (`./wallet --keyfile <path> --connect <host> <port>`
prints its address on startup; it doesn't need a live node to connect to for
address generation alone). The node itself never sees or needs the private
key -- that's the whole point of `--reward-address` taking a plain address
string, not a keyfile. The private keys for these four demo addresses
aren't checked in anywhere; they were one-off generated to populate this
compose file. If you want to actually spend what one of these nodes mines,
generate your own wallet, take its printed address, and substitute it into
that node's `--reward-address` in `docker-compose.yml` before starting the
harness.

## Running it

From the repository root:

```bash
docker compose up --build
```

This builds the image (see `Dockerfile`) and starts all 4 containers. Each
node's logs stream to your terminal, prefixed with the container name
(`forgechain-node1-1`, etc. -- the exact prefix depends on your Docker
Compose version).

To run in the background and view logs separately:

```bash
docker compose up --build -d
docker compose logs -f
```

To view a single node's logs:

```bash
docker compose logs -f node1
```

To check a node's balance or chain height without attaching to its stdin
(each node's own interactive command loop reads container stdin, which
`docker compose logs` doesn't expose), the RPC channel isn't enabled by
default here -- add `--rpc-port <PORT>` to a service's `command` in
`docker-compose.yml` and publish that port to query it from the host with
`nc` (see `docs/cli-usage.md` §3 for the RPC protocol).

## What to look for

Each node logs in the same categorized format used by `Orchestrator`
(`[BOOT]`, `[PEER]`, `[MINE]`, etc.). Watch for:

- `[PEER] TCP connect + handshake succeeded` on node2/3/4 -- confirms the
  chain topology connected successfully.
- `[MINE] block ACCEPTED, height now ...` lines on every node as it mines
  its own blocks and relays/accepts others'.
- All four nodes' heights should converge to the same value and stay in
  sync as mining continues -- the heavier chain (by cumulative work) wins,
  same as in `NodeReorg*` tests.

## Stopping and cleaning up

```bash
docker compose down
```

Add `-v` to also remove the network:

```bash
docker compose down -v
```

## Known limitations

- This harness demonstrates propagation/sync across real, isolated
  processes -- it does not yet exercise fork resolution or reorg. That
  requires deliberately partitioning the network (e.g. via
  `docker network disconnect`) so two groups of nodes mine independently
  before reconnecting -- see the network-partition-test issue for that
  follow-up.
- No `wallet` container is included here; sending a transaction between two
  demo nodes currently means running `wallet` on the host against a
  published RPC port (see above).
