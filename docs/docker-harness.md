# Docker multi-node harness

Runs 4 isolated `forgechain` node containers on their own Docker network, so
you can observe propagation, sync, and (later) fork resolution across real,
separate processes -- not multiple `Node` objects sharing one process
(`PropagationTest.cpp`), and not two terminals sharing one host's loopback
interface (`peer_a`/`peer_b` demo, see `src/app/peer_node_demo.cpp`).

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

Only `node1` mines by default. Blocks and transactions it produces should
propagate outward through the chain to node2, node3, and node4 via the same
INV/GETDATA/BLOCK relay logic exercised in `PropagationTest.cpp`.

Nodes connect to each other using **fixed IP addresses** (`172.28.0.11`
through `172.28.0.14`), not Docker's service-name DNS. This is because
`connect_to` (`src/network/TcpSocket.cpp`) uses `inet_pton`, which only
accepts literal IP addresses -- it does not resolve hostnames. If you add
more nodes, assign them the next free IP in the `172.28.0.0/24` subnet and
update `docker-compose.yml`'s `ipv4_address` fields accordingly.

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

## What to look for

Each node's log uses the same categorized format as the two-terminal demo
(`[BOOT]`, `[PEER]`, `[CHAIN]`, `[MEMPOOL]`, `[MINE]`, `[STATUS]`,
`[SHUTDOWN]` -- see `src/app/peer_node_demo.cpp`'s header comment for
details). Watch for:

- `[PEER] TCP connect + handshake succeeded` on node2/3/4 -- confirms the
  chain topology connected successfully.
- `[MINE]` lines on node1 as it mines new blocks.
- `[CHAIN] height ... -> ...` lines appearing on node2, then node3, then
  node4 shortly after each node1 `[MINE]` line -- confirms propagation is
  relaying block-by-block down the chain, not just to node1's immediate
  peer.
- All four nodes' `[CHAIN]` height should converge to the same value and
  stay in sync as node1 keeps mining.

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
- All nodes here mine with the same demo wallet content pattern as
  `peer_node_demo.cpp` (see that file for why: `Node` has no public
  "mine and broadcast" API, so mining is simulated by injecting a
  self-connected raw peer).
