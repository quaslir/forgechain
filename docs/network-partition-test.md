# Network partition test (issue #48)

This is the test that actually exercises fork resolution/reorg end to end
-- unlike `docker-compose.yml` (issue #47), where all 4 nodes stay
connected and simply keep extending one shared chain.

## Setup

Two independent pairs of nodes that never connect to each other:

```
node1 (mines every 10s)          node3 (mines every 11s)
  ^                                 ^
  | node2 connects to node1         | node4 connects to node3
node2 (mines every 12s)          node4 (mines every 13s)
```

Both pairs start from the same genesis block (it's deterministic --
`Block{1, HashBytes{}, 0, {}}` -- see `src/core/Blockchain.cpp`), but since
they never communicate, they will independently mine two different chain
histories.

## Running it

```bash
cd forgechain   # or wherever your clone lives
docker compose -f docker-compose.partition.yml up --build
```

Let it run for **at least 2-3 minutes** so both pairs mine several blocks
each and clearly diverge (you'll see different block hashes at the same
height in each pair's logs -- e.g. node1/node2 at height 5 will have a
different hash than node3/node4 at height 5).

## Bridging the two groups

Once you're satisfied the two groups have diverged, manually connect them.
`Node::connect_to_peer` is only called once at startup (see
`src/app/peer_node_demo.cpp`) -- there's no live "connect now" command --
so the simplest way to bridge them is to restart one node with an added
`--connect` pointing at the other group.

In a new terminal, stop and restart node3 with a bridge to node1:

```bash
docker stop forgechain-partition-node3
docker run --rm \
  --name forgechain-partition-node3 \
  --network forgechain-partition-yml_forgechain-partition-net \
  --ip 172.29.0.13 \
  forgechain-partition-yml-node3 \
  --port 9000 --connect 172.29.0.11 9000 --mine-every 11
```

(The exact network/image name Docker Compose generates depends on your
repository's directory name -- run `docker network ls` and `docker images`
beforehand to confirm the actual names if the above doesn't match.)

node4 will notice node3 died (heartbeat/`peer_count DROPPED`) but doesn't
need to reconnect itself -- once node3 rejoins and syncs with node1's
group, node3 will relay the winning chain to node4 via normal propagation,
the same way any other block relay works.

## What to look for

- After the bridge connects, watch for `[PEER] TCP connect + handshake
  succeeded` on the restarted node3.
- `Node::register_new_peer` compares chain heights at handshake time and
  sends `GETBLOCKS` if one side is behind (`src/network/Node.cpp`) -- so
  you should see a burst of `[CHAIN] height X -> Y` lines as node3 catches
  up, possibly jumping several heights at once (that's the GETBLOCKS/IBD
  path from issue #33-adjacent work, not simple one-block-at-a-time
  propagation).
- Depending on which side is heavier (more accumulated `cumulative_work_`,
  not just more blocks -- see `docs/fork-resolution.md`), you should see
  either:
  - node3 quietly extends its own history if node1's side turns out
    lighter (no reorg -- node3's own chain simply wins by continuing to
    grow), or
  - an explicit `[REORG] switched to heavier branch: discarded N block(s),
    applied M block(s), new tip=...` line on whichever node's
    `Node::try_reorg` actually performs the switch (see
    `src/network/Node.cpp`) -- this is the definitive signal a real fork
    resolution happened, not just ordinary propagation.
- Watch node4's log too: once node3 has adopted the winning chain, node4
  should receive it via ordinary block-by-block propagation from node3,
  the same as any other relay.
- Eventually all four nodes' `[CHAIN]` height and latest hash should
  converge to the same value, and any transactions unique to the losing
  side's discarded blocks should reappear in mempool shortly after the
  `[REORG]` line (see `Node::try_reorg`'s mempool-reinsertion logic).

## Known limitations

- The manual restart-with-`--connect` bridging step is deliberately manual,
  not scripted, since the whole point of this test is to let a human
  operator control exactly when the reconnection happens and watch what
  follows in real time.
- The `[REORG]` log line is printed directly to stderr from
  `Node::try_reorg` (not through `peer_node_demo.cpp`'s categorized
  `DemoLog`), since `Node` has no logger parameter -- it will appear
  interleaved with the demo's own `[BOOT]`/`[CHAIN]`/etc. lines but in a
  slightly different format (`[NODE:PORT] [REORG] ...` instead of
  `[HH:MM:SS.mmm] [NODE:PORT] [REORG] ...`).