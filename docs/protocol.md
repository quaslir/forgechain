# forgechain P2P Protocol

This document specifies the wire format used for communication between
forgechain nodes, and the consensus rules every node applies to the blocks
it receives. It is the reference for implementing the networking layer and
for anything that produces or validates blocks.

All multi-byte integers are encoded **little-endian** unless stated
otherwise, matching the host byte order this project already assumes
elsewhere (the existing `Block`/`Transaction` serialization writes raw
struct bytes directly via `reinterpret_cast`, which is little-endian on
the x86-64/ARM64 platforms this project targets).

## 1. Message framing

Every message sent over a TCP connection has the same fixed-size header,
followed by a variable-length payload:

```
+------------+----------+------------------+-------------------------+
| magic (4B) | cmd (1B) | payload_len (4B) | payload (payload_len B) |
+------------+----------+------------------+-------------------------+
```

| Field | Size | Type | Description |
|---|---|---|---|
| `magic` | 4 bytes | raw bytes | Fixed value `0x46 0x52 0x47 0x43` (ASCII `"FRGC"`). Identifies the byte stream as a forgechain message; a receiver that doesn't see this at the start of a message discards the connection as garbage/foreign traffic. |
| `cmd` | 1 byte | `uint8` | Command type — see §2. |
| `payload_len` | 4 bytes | `uint32` | Number of bytes in `payload` that follow. Lets the receiver know exactly how many more bytes to read before the next message header begins. |
| `payload` | `payload_len` bytes | raw bytes | Command-specific content — see §3. |

Header size is fixed at **9 bytes**. A receiver always reads exactly 9
bytes first, validates `magic`, reads `cmd` and `payload_len`, then reads
exactly `payload_len` more bytes before starting to parse the next
message.

### Sanity limits

`payload_len` is checked against `MAX_PAYLOAD_SIZE` (16 MiB) before a
receive buffer is allocated, on both the sending and the receiving side.
An unvalidated, attacker-controlled length field is a classic
memory-exhaustion vector; a message announcing a larger payload is treated
as a protocol violation and the connection is dropped.

## 2. Command codes

| Code | Name | Payload | Purpose |
|---|---|---|---|
| `0x00` | `VERSION` | §3.1 | Handshake — announce protocol version and chain height on connect. |
| `0x01` | `INV` | §3.2 | Announce that the sender has a new block or transaction (by hash only). |
| `0x02` | `GETDATA` | §3.2 | Request the full contents of a previously announced `INV` item. |
| `0x03` | `BLOCK` | §3.3 | A full block, sent in response to `GETDATA` or `GETBLOCKS`. |
| `0x04` | `TX` | §3.4 | A full transaction, sent in response to `GETDATA`. |
| `0x05` | `GETBLOCKS` | §3.5 | Request blocks from a given height onward (sync). |
| `0x06` | `PING` | §3.6 | Liveness check. |
| `0x07` | `PONG` | §3.6 | Response to `PING`. |
| `0x08` | `PEERS` | §3.7 | Share known, reachability-verified peer addresses. |

Codes `0x09`–`0xFF` are reserved for future message types.

## 3. Payload formats

### 3.1 `VERSION`

Sent immediately after a TCP connection is established, before any other
message is processed (see §4 for the handshake sequence).

| Field | Size | Type | Description |
|---|---|---|---|
| `protocol_version` | 4 bytes | `uint32` | Version of this protocol spec the sender implements. Starts at `1`. |
| `chain_height` | 8 bytes | `uint64` | Height of the sender's current best chain (number of blocks, including genesis). |
| `timestamp` | 8 bytes | `uint64` | Sender's current Unix timestamp (seconds). |
| `listen_port` | 2 bytes | `uint16` | The port this node accepts inbound connections on. Needed because an inbound connection's source port is ephemeral and says nothing about where the peer can be reached. A node that does not accept inbound connections sends `0`. |
| `node_id` | 8 bytes | `uint64` | Randomly generated per node instance. Identifies the node independently of its address — see §4.1 and §6. |

Total: 30 bytes.

### 3.2 `INV` / `GETDATA`

Both messages share the same payload shape — `INV` announces, `GETDATA`
requests. A single message can list more than one item (batched), so the
payload is a count followed by that many entries.

| Field | Size | Type | Description |
|---|---|---|---|
| `item_count` | 4 bytes | `uint32` | Number of entries that follow. |
| entries | `item_count * 33` bytes | array | See below, repeated `item_count` times. |

Each entry:

| Field | Size | Type | Description |
|---|---|---|---|
| `item_type` | 1 byte | `uint8` | `0` = block, `1` = transaction. |
| `item_hash` | 32 bytes | raw bytes | The block's hash, or the transaction's identifying hash: `double_sha_256` of the transaction's signing serialization (every field of §3.4 except `signature_len` and `signature`). The signature is deliberately excluded, so the identifier cannot be changed by re-encoding the signature. |

### 3.3 `BLOCK`

The full serialized block, so the receiver can independently validate
and store it — not just the header, but every transaction inside.

| Field | Size | Type | Description |
|---|---|---|---|
| `version` | 4 bytes | `uint32` | Matches `Block::version_`. |
| `prev_hash` | 32 bytes | raw bytes | Matches `Block::prev_hash_`. |
| `merkle_root` | 32 bytes | raw bytes | Matches `Block::merkle_root_`. |
| `timestamp` | 8 bytes | `uint64` | Matches `Block::timestamp_`. Constrained by §8.3. |
| `difficulty` | 4 bytes | `uint32` | Matches `Block::difficulty_`: the number of leading zero bits the block hash must have. Must equal the value consensus requires at the block's height — see §8.2. |
| `nonce` | 4 bytes | `uint32` | Matches `Block::nonce_`. |
| `tx_count` | 4 bytes | `uint32` | Number of transactions that follow. |
| transactions | variable | array | `tx_count` entries, each a `tx_len` (4 bytes, `uint32`) followed by `tx_len` bytes of a transaction in `TX` payload format (§3.4). |

The block hash itself is **not transmitted**. The receiver recomputes it
from the header fields on deserialization, so a sender cannot present a
block under a hash that doesn't match its content; what arrives is always
a header whose real hash either satisfies its declared difficulty or
doesn't (§8.4, step 1).

The per-transaction `tx_len` prefix makes each transaction independently
delimited. This is what makes the wire format fully **parseable** — a
plain concatenation of transactions would be sufficient for hashing but
not for unambiguous decoding.

### 3.4 `TX`

| Field | Size | Type | Description |
|---|---|---|---|
| `amount` | 8 bytes | `uint64` | `Transaction::amount_`. |
| `sender_len` | 4 bytes | `uint32` | Byte length of `sender`. |
| `sender` | `sender_len` bytes | UTF-8 string | `Transaction::sender_`. |
| `recipient_len` | 4 bytes | `uint32` | Byte length of `recipient`. |
| `recipient` | `recipient_len` bytes | UTF-8 string | `Transaction::recipient_`. |
| `public_key_len` | 4 bytes | `uint32` | Byte length of `public_key`. |
| `public_key` | `public_key_len` bytes | raw bytes | `Transaction::sender_public_key_`. The receiver verifies that it hashes to `sender` and that `signature` is valid under it. Empty for the coinbase transaction. |
| `fee` | 8 bytes | `uint64` | `Transaction::fee_`. Collected by the miner of the block that includes the transaction. |
| `signature_len` | 4 bytes | `uint32` | Byte length of `signature`. |
| `signature` | `signature_len` bytes | raw bytes | `Transaction::signature_` (DER-encoded ECDSA signature, variable length — see `crypto::sign`). Empty for the coinbase transaction. |

Every variable-length field is length-prefixed, so the encoding is
unambiguously parseable — e.g. `sender="ab", recipient="c"` is
distinguishable from `sender="a", recipient="bc"`. A payload with trailing
bytes after `signature` is rejected.

### 3.5 `GETBLOCKS`

| Field | Size | Type | Description |
|---|---|---|---|
| `from_height` | 8 bytes | `uint64` | Height the requester wants blocks from. |

The responder replies with one `BLOCK` message per block, starting at
`from_height`, up to its chain tip or `MAX_BLOCKS_PER_RESPONSE` (2000)
blocks, whichever comes first. If `from_height` is at or above the
responder's tip, it sends nothing.

The cap keeps a single request from turning into an unbounded burst of
traffic. A requester that is further behind simply asks again: `GETBLOCKS`
is sent both after a handshake with a taller peer (§4) and periodically
to one connected peer at a time, rotating through them. The periodic
request also recovers blocks missed on a live connection, such as an `INV`
that was lost or a block that arrived before the clock allowed it
(§8.3).

Note: this is a height, not a hash. Sending a height is simpler but
assumes both sides agree on the chain below that point; a hash-based
locator would be more robust against forks and is a candidate for a
future protocol version.

### 3.6 `PING` / `PONG`

Both carry an **empty payload** (`payload_len = 0`). A node sends `PING`
to a peer that has been silent for longer than `PING_INTERVAL` and marks
it dead once silence exceeds `PING_TIMEOUT`; any received message counts
as liveness evidence, so no nonce correlation is needed.

### 3.7 `PEERS`

Carries a list of peer addresses. Sent unsolicited after a connection is
established (§5.1); there is no request message for it.

| Field | Size | Type | Description |
|---|---|---|---|
| `count` | 4 bytes | `uint32` | Number of entries that follow. |
| entries | variable | array | `count` length-delimited entries, see below. |

Each entry:

| Field | Size | Type | Description |
|---|---|---|---|
| `entry_len` | 4 bytes | `uint32` | Byte length of the encoded address that follows. |
| `port` | 2 bytes | `uint16` | The peer's `listen_port`. |
| `host_len` | 4 bytes | `uint32` | Byte length of `host`. |
| `host` | `host_len` bytes | ASCII string | Dotted-quad IPv4 literal, e.g. `"13.51.48.228"`. |

The outer `entry_len` makes each entry independently skippable, so a
malformed or unrecognised entry does not desynchronise parsing of the
rest of the list.

**Which addresses may be sent is constrained — see §5.1.** A receiver
must not assume the sender followed those rules and applies its own
filtering (§5.2) regardless.

## 4. Handshake sequence

When node A connects to node B:

```
A                                   B
|---------- VERSION --------------->|  A announces its protocol version + chain height
|<--------- VERSION ----------------|  B replies with its own
|                                   |
|   (both sides now know whether    |
|    they're on a compatible        |
|    protocol version, and who's    |
|    ahead in chain height)         |
|                                   |
|<--------- GETBLOCKS --------------|  if B is behind A, B requests sync
|---------- BLOCK ----------------->|  A sends the missing blocks directly,
|---------- BLOCK ----------------->|  one per message, up to the §3.5 cap
|             ...                   |
```

Rules:

- No message other than `VERSION` is processed until both sides have
  exchanged `VERSION`. A node receiving any other message type first
  should close the connection.
- If `protocol_version` values are incompatible (exact compatibility
  policy — e.g. exact match vs. minimum supported version — is an
  implementation decision, not fixed here), the connection is closed
  after the version exchange rather than left half-negotiated.
- Whichever side has the lower `chain_height` is expected to initiate
  `GETBLOCKS`; the side with the higher height simply waits to see if a
  sync request arrives.
- Sync responses skip `INV`/`GETDATA`: the requester has already said
  which blocks it wants by height, so announcing them first would only add
  a round trip. `INV` is for blocks the receiver has not asked for (§5).

### 4.1 Self-connection and crossed connections

Because addresses propagate via `PEERS`, a node can end up dialing its
own advertised address. After the `VERSION` exchange, a node that sees
its own `node_id` in the peer's `VERSION` closes the connection. An
address-based check is not sufficient: a node does not reliably know its
own public address.

Two nodes can also dial each other simultaneously, producing two TCP
connections between the same pair. Both sides detect this by finding an
existing live peer with the same `host` and `listen_port`, and must
independently arrive at the *same* verdict about which connection
survives, or they will either both drop (leaving the pair disconnected)
or both keep (leaving a redundant link). The rule is:

```
keep_new = (my_node_id < their_node_id) == connection_is_outbound
```

Since exactly one side of any given connection sees it as outbound, and
the `node_id` comparison is antisymmetric, both nodes select the same
surviving connection. The losing connection is closed by whichever side
holds it; the peer entry is marked dead and reaped asynchronously rather
than being replaced in place.

## 5. Propagation (gossip)

After the initial handshake and sync, new blocks and transactions are
announced opportunistically:

1. A node that mines a new block, or receives a new transaction into its
   mempool, sends `INV` to all connected peers (one entry, `item_type=0`
   for a block or `1` for a transaction).
2. A peer receiving `INV` checks whether it already has that hash (in
   its chain, or its mempool). If not, it replies with `GETDATA` for
   that specific item.
3. The original sender responds with `BLOCK` or `TX` containing the full
   data.
4. The receiving peer validates the data before accepting it — for a
   transaction: signature, and that the sender can afford it; for a block:
   every rule in §8.4 — and only then re-announces it via `INV` to its
   *other* peers (excluding the one it received it from). This is what
   makes propagation reach the whole network, not just the direct
   neighbor, while guaranteeing that a node never relays a block it
   would not itself accept.

A node must not re-broadcast the same `INV` for an item it has already
announced recently, to avoid redundant gossip storms — the exact
de-duplication window/mechanism is an implementation detail.

### 5.1 Peer discovery

Addresses spread through the network the same way blocks do, but under a
stricter rule about what may be shared.

**Only reachability-verified addresses are advertised.** A node may
include an address in a `PEERS` message only if it has itself
successfully completed an outbound connection to that exact `host:port`.
Addresses learned from gossip but never dialed, and addresses of inbound
peers, are never forwarded.

This restriction exists because an inbound connection tells a node almost
nothing about how to reach that peer. The source address is where the
connection came *from*, and the accompanying `listen_port` is
self-reported. For a peer behind NAT the resulting pair is not dialable
by anyone — but without this rule it would still be gossiped network-wide,
and every node receiving it would retry it indefinitely. A single laptop
joining from a home connection is enough to make the whole network waste
connection attempts forever.

The consequence is asymmetric and intentional: a node behind NAT
participates fully — it dials out, syncs, relays blocks — but its address
is never advertised, so nobody attempts to dial it.

`PEERS` is sent in two situations:

1. To a newly connected peer, carrying this node's verified address set.
2. To existing peers when a *new outbound* connection is established,
   carrying just that one address. Inbound connections trigger no such
   announcement, per the rule above.

### 5.2 Receiving `PEERS`

A node receiving `PEERS` records the addresses and does not act on them
immediately. Each address is filtered before being stored; an address is
rejected outright if it is:

- port `0`, or an empty or non-IPv4-literal host
- loopback (`127.0.0.0/8`) or this-network (`0.0.0.0/8`)
- private per RFC1918 (`10/8`, `172.16/12`, `192.168/16`)
- link-local (`169.254/16`)
- multicast or reserved (`224.0.0.0` and above)

Storage is capped, and duplicate entries are ignored. Both limits matter:
`PEERS` payloads are attacker-controlled, so an unbounded address store
is a memory-exhaustion vector, and unfiltered entries would let a
malicious peer direct every node in the network to scan private address
ranges inside its host's datacenter.

Recording an address and dialing it are deliberately separate steps.
Handling a `PEERS` message must not block: it is processed on the
connection's message-reading path, and a blocking `connect()` there would
stall block and transaction relay for that peer for the duration of a TCP
timeout.

### 5.3 Connection policy

Dialing is driven independently of message handling. A node maintains a
target number of outbound connections and, while below it, selects stored
addresses that are not already connected and are due for an attempt.

Failed attempts are retried with exponential backoff, capped, and
abandoned after a bounded number of consecutive failures. Backoff is what
makes an unreachable address — a peer that has gone down, or one behind
NAT that was learned before this rule existed — cheap to keep in the
store rather than a source of continuous connection attempts.

A successful outbound connection marks that address verified, which is
what makes it eligible for advertisement under §5.1. Addresses supplied
at startup are stored the same way as gossiped ones, so a node that
starts before its configured peers retries them rather than giving up.

## 6. Design notes and open decisions

- **Connection model**: a single bidirectional TCP connection per peer
  pair. Simultaneous dials are resolved to one connection by §4.1.
- **Peer identification**: peers are addressed by `host:listen_port` and
  identified for the duration of a session by `node_id` (§3.1). The
  `node_id` is regenerated on restart and is not authenticated — it
  disambiguates connections, nothing more. It is not a persistent
  identity and provides no defence against a peer claiming an arbitrary
  value; a public-key identity would, and remains out of scope.
- **Sybil resistance**: none. There is no peer scoring, no banning, no
  rate limiting, and no diversity requirement across address ranges when
  selecting peers to dial. A single actor controlling many addresses can
  fill a node's address store and monopolise its outbound connections
  (an eclipse attack). The filtering in §5.2 bounds memory use; it does
  not bound influence.
- **Address persistence**: the address store is in-memory only and is
  lost on restart, so a node relies on its configured bootstrap peers
  every time it starts.
- **Timeouts and reconnection**: `PING`/`PONG` supports the liveness
  policy; connection retry policy is described in §5.3.
- **Maximum message size**: 16 MiB, see §1.

## 7. Fork resolution and reorganization

When a node receives a `BLOCK` whose `prev_hash_` does not match its
current chain tip, this is not necessarily invalid data — it may be the
start of a competing branch (fork). This section describes how a node
detects, evaluates, and (if warranted) switches to such a branch. The full
rationale for the weight metric is in `docs/fork-resolution.md`; this
section covers how it fits into the message-handling flow described in
§5 above.

### 7.1 Fork detection

Before classification, every block must pass the context-free checks of
§8.4 (proof of work against its declared difficulty, coinbase rules).
A node then classifies it into exactly one of three outcomes
(`Blockchain::classify_new_block`):

- **Invalid** — the block's hash does not match a fresh `compute_hash()`
  over its own fields, or a coinbase transaction appears anywhere other
  than first. Over the wire the hash is always recomputed on receipt
  (§3.3), so in practice this outcome guards blocks constructed locally
  or passed in from outside the P2P layer. The block is dropped and never
  considered for propagation or fork resolution, regardless of what its
  `prev_hash_` claims.
- **Valid** — the block is well-formed and `prev_hash_` matches the
  current chain tip. It is checked against the contextual consensus rules
  (§8.4, step 3), appended to the chain if it passes, and re-announced
  via `INV` as in §5.
- **ForkCandidate** — the block is well-formed, but `prev_hash_` points
  at something other than the current tip. This is a potential competing
  branch, not garbage; it is *not* re-announced immediately (the node
  hasn't accepted it into its own chain yet), and is instead held for
  evaluation as described next.

Order matters: content authenticity is always checked before the
`prev_hash_` comparison, so a forged block can never be misclassified as a
fork candidate just because it happens to carry an unrelated
`prev_hash_`.

### 7.2 Orphan pool

A `ForkCandidate` block is stored in a per-node orphan pool, keyed by its
own hash (`OrphanPool::add_orphan`), rather than discarded. This allows a
multi-block competing branch to be assembled incrementally as its blocks
arrive out of order or across multiple `BLOCK` messages, without requiring
all of them to arrive before any progress can be evaluated.

Blocks can arrive in **any** order, including a child before its parent.
When a block's own path back to the main chain cannot be completed
(§7.3), the node answers with a `GETDATA` for the missing parent. When a
block *does* connect, the node also looks **forward** through the pool
for descendants that arrived earlier and were waiting on it, following
children up to `kMaxForkDepth` blocks. Every branch end reachable this way
is a candidate tip for the weight comparison in §7.4. Without the forward
walk, a branch whose blocks arrived in reverse order would sit in the pool
forever, even though it is complete and heavier.

After a successful reorganization, every block that entered the chain is
removed from the pool. Blocks on branches that lost stay in the pool.

### 7.3 Locating the common ancestor

After a block is added to the orphan pool, the node attempts to trace a
path from that block backward through `prev_hash_` links — through other
orphan-pool entries if necessary — until it reconnects with a block
already present in the node's own chain (`build_fork_chain`). This
reconnection point is the *common ancestor*: where the competing branch
diverges from the node's current history.

This walk is capped at **`kMaxForkDepth` (100 blocks)** — if no
connection to the main chain is found within that many hops, the branch
is treated as still-incomplete (more blocks may arrive later) rather than
immediately retried on every subsequent block. This cap exists because an
arbitrarily deep claimed fork (e.g. branching from hundreds of blocks
ago) is far more likely to indicate an attack or a wildly stale/malicious
peer than ordinary network latency — legitimate forks in this network
are expected to be shallow, since block propagation (§5) is fast relative
to block production. A fork this deep is rejected outright: `nullopt` is
returned, the node's chain is left untouched, and no further work is
attempted on that branch until it either reaches the cap again with a
different/updated tip or is superseded by unrelated activity.

Note that the cap limits the length of the *branch*, not how many
main-chain blocks a branch can replace. What prevents a short branch from
cheaply rewriting deep history is §8.5: every branch block must carry the
difficulty consensus requires at its height.

### 7.4 Weight comparison

For each candidate tip found in §7.2, the node builds the full branch back
to the common ancestor and checks it against the consensus rules (§8.5).
**Branches that fail are discarded before any weight is compared** — an
invalid branch never wins, however much work it claims. Among the
remaining branches the node picks the one with the most accumulated work,
and compares that against its own chain tip (`fork_work`,
`is_fork_heavier`):

```
branch_work = common_ancestor.cumulative_work_
            + sum(block_work(b.difficulty_) for b in branch_blocks)

switch_to_branch = branch_work > chain.latest().cumulative_work_
```

Where `block_work(difficulty) = 2^difficulty` (see `docs/fork-resolution.md`
§3 for the full derivation from PoW target probability). The comparison is
**strict** (`>`, not `>=`): a tie does not trigger a switch, to avoid a
node flapping between two equally-heavy chains as new blocks trickle in
from each side.

Selecting the heaviest valid branch first, and reorganizing once, matters
when a block has more than one descendant in the pool: trying branches one
by one could reorganize onto a lighter branch and then immediately back
off it, rolling the ledger back twice and announcing blocks that were
never meant to stay.

This is why the common ancestor block itself is carried alongside the
branch (rather than the branch's blocks being compared in isolation) —
orphan-pool blocks never pass through the normal chain-append path and so
never accumulate a meaningful `cumulative_work_` of their own; the
ancestor's already-correct value from the main chain is the necessary
starting point for an apples-to-apples comparison.

Inside one difficulty epoch (§8.2) all blocks at a given height carry the
same difficulty, so there "heavier" reduces to "longer". A shorter branch
can only be heavier across a retarget boundary, when its epoch's
timestamps earned it a higher difficulty.

### 7.5 Reorganization procedure

If the competing branch is heavier, the node performs a reorganization
(`Blockchain::reorganize_to`):

1. Locate the common ancestor's height in the current chain.
2. Discard every block after that height (the losing branch).
3. Append the competing branch's blocks on top, in order, exactly as if
   each had arrived individually via normal block acceptance (§7.1) —
   this recomputes `cumulative_work_` correctly for each newly-applied
   block.

The blocks discarded in step 2 are returned to the caller so their
transactions can be handled — see §7.6.

### 7.6 Mempool reinsertion

Transactions that were only present in the discarded (losing) blocks are
not simply lost: each is re-inserted into the node's mempool, so it can be
mined again in a future block, **unless** that same transaction (by hash)
is also present somewhere in the newly-adopted branch — which can happen
if the transaction had independently propagated to and been mined by
whichever peer produced the winning branch. Re-inserting a transaction
that's already confirmed in the adopted chain would risk it being mined a
second time, effectively double-spending the same funds.

### 7.7 Propagation after a reorg

Once a reorganization completes, the newly-adopted branch's blocks are
announced via `INV` (§5) to the node's peers, one per block in the branch
(oldest first) — not just the new tip. This ensures peers that don't
already share any of the intermediate blocks can still request and adopt
each one in turn via ordinary `GETDATA`/`BLOCK` exchange, rather than
receiving only the final tip and being unable to link it back to their
own chain.

## 8. Consensus rules

A block is valid only if it satisfies every rule in this section. The
rules are a function of the block and the chain it builds on — never of
anything the block's producer declares about itself — so every honest
node reaches the same verdict about the same block.

### 8.1 Parameters

| Parameter | Constant | Value | Meaning |
|---|---|---|---|
| Initial difficulty | `initial_difficulty` | 15 bits | Required difficulty until the first adjustment. |
| Minimum difficulty | `min_difficulty` | 8 bits | Floor; an adjustment never goes below it. |
| Target block time | `target_block_time` | 10 s | Block interval the adjustment aims for. |
| Retarget interval | `retarget_interval` (`N`) | 20 blocks | Length of a difficulty epoch. |
| Maximum adjustment | `kMaxStepBits` | 2 bits | Largest change in one adjustment, in either direction. |
| MTP window | `mtp_window` | 11 blocks | Blocks considered for the median time past. |
| Future drift | `max_future_drift` | 120 s | How far ahead of the validating node's clock a timestamp may be. |
| Block reward | `mining_reward` | 50 | Maximum coinbase amount, before fees. |

The values are held in `consensus::kMainParams`. They are part of
consensus: nodes with different values disagree about which blocks are
valid and split into separate networks.

Throughout this section, *height* `h` is the height of the block being
validated or produced; its parent is at `h - 1`. Genesis is height 0.

### 8.2 Required difficulty

The difficulty a block at height `h` must declare is
`next_difficulty(h)`:

1. **First epochs.** If `h <= N`, the required difficulty is
   `initial_difficulty`. Heights between `N` and `2N` inherit it from
   their parent by rule 2, so the first adjustment happens at height `2N`.
   This keeps genesis — whose timestamp is `0` — out of every adjustment
   window.
2. **Inside an epoch.** If `h` is not a multiple of `N`, the required
   difficulty equals the parent's difficulty.
3. **At an epoch boundary** (`h % N == 0`), difficulty is recalculated
   from the previous epoch:

   ```
   first    = timestamp(h - N)
   last     = timestamp(h - 1)
   actual   = last > first ? last - first : 0     (0 is treated as 1)
   expected = (N - 1) * target_block_time

   delta    = round(log2(expected / actual)), clamped to [-2, +2]
   required = max(parent.difficulty + delta, min_difficulty)
   ```

   The window spans `N` blocks and therefore `N - 1` intervals, which is
   why `expected` uses `N - 1`. Using `N` would bias every adjustment
   slightly downward — the same off-by-one that exists in Bitcoin's
   original retarget.

Blocks came in twice as fast as intended → `delta = +1`, one more
leading zero bit, twice the expected work. Twice as slow → `delta = -1`.
The clamp bounds how far a single epoch — including one with manipulated
timestamps — can move difficulty.

**Granularity.** Difficulty is a whole number of leading zero bits, so
each step changes expected work by at least a factor of two. When the
network's actual hash rate sits between two bit values, difficulty may
alternate between them from epoch to epoch; average block time still
stays close to target. A full-precision 256-bit target would remove this
and is a candidate for a future protocol version.

### 8.3 Timestamps

Because difficulty is derived from timestamps, timestamps are consensus
data and are bounded on both sides.

**Median time past (lower bound).** Take the timestamps of the up to
`mtp_window` blocks immediately below `h` (heights
`max(0, h - mtp_window)` through `h - 1`), sort them ascending, and take
the element at index `count / 2`. A block's timestamp must be **strictly
greater** than this median.

A median rather than the parent's timestamp is used so that a block may
legitimately carry a timestamp slightly earlier than its parent's — clocks
across the network are never exactly in sync — while the chain's time as
a whole still cannot move backwards.

**Future bound (upper bound).** A block's timestamp must be at most
`max_future_drift` seconds ahead of the **validating node's** clock.

Unlike every other rule, this one depends on local state, so the same
block can be rejected by one node and accepted by another, or rejected
now and accepted a minute later. A block rejected for this reason is not
permanently invalid: it becomes acceptable once the node's clock catches
up, and the periodic `GETBLOCKS` request (§3.5) will deliver it again.
Implementations that add peer penalties must not penalise a peer for a
block rejected only by this rule.

Nodes whose clocks differ by more than `max_future_drift` will reject each
other's fresh blocks. Node clocks are expected to be kept synchronized.

### 8.4 Validation of a block

A node applies the checks in this order; the first failure rejects the
block.

1. **Proof of work.** The block's hash has at least `difficulty` leading
   zero bits. This is checked against the difficulty the block
   *declares*, so it can run before any chain state is consulted and
   cheaply filters out random data.
2. **Coinbase.** At most one coinbase transaction (sender
   `kCoinbaseSender`), and if present it is the first transaction. Its
   amount is at most `mining_reward` plus the sum of the fees of every
   other transaction in the block.
3. **Contextual rules**, evaluated against the chain the block builds on:
   - timestamp above the median time past and within the future bound
     (§8.3);
   - declared `difficulty` **equal to** the required difficulty at the
     block's height (§8.2) — not merely at least it. Step 1 proves the
     block did the work it declares; this step proves it declares the
     work consensus requires. Only together do they mean anything.
4. **Transactions.** Every transaction applies to the ledger in order:
   valid signature, and the sender can afford `amount + fee`. If any
   transaction fails, the whole block is rejected and the ledger is left
   untouched.

For a block extending the tip, "the chain it builds on" is the node's
current chain. For a block on a competing branch, see §8.5.

### 8.5 Validation of a competing branch

A branch assembled in §7.3 is validated block by block, oldest first,
before it may take part in the weight comparison (§7.4). Each block is
checked against step 3 of §8.4 **using the branch's own history**:

- heights up to and including the common ancestor are read from the
  node's chain, which the branch shares;
- heights above the common ancestor are read from the branch itself.

Reading the node's chain above the common ancestor would be wrong: those
heights hold the *other* branch's blocks, with their own timestamps and
difficulties. A branch whose recent blocks came in faster than the main
chain's legitimately requires a higher difficulty at its next epoch
boundary, and a branch with earlier timestamps is judged against its own
median, not the main chain's.

Validation stops at the first block that fails. The blocks before it —
the branch's valid prefix — remain a candidate on their own; the failing
block and everything after it are not. Discarding the whole branch instead
would let anyone block a reorganization for free: a child with
`difficulty = 0` costs nothing to mine, and hung off the tip of an honest
branch it would turn that branch's only candidate tip invalid.

This rule is what makes difficulty meaningful for fork resolution. Without
it, a single block declaring a high difficulty — genuinely mined at that
difficulty — would carry `2^difficulty` work and could replace an
arbitrary number of honest main-chain blocks behind a shallow common
ancestor. With it, a branch can only accumulate work at the rate the
chain's own history allows.

### 8.6 Producing a block

A miner builds a block from a template taken under one consistent view of
the chain (`ChainManager::block_template`):

- `prev_hash` — the current tip;
- `difficulty` — the required difficulty at the next height (§8.2);
- `timestamp` — `max(now, median_time_past + 1)`;
- transactions — mempool transactions that apply to the ledger in order.

The timestamp rule matters whenever several blocks are produced within
the same second, which happens routinely at low difficulty: a plain
wall-clock timestamp would repeat, the median would catch up with it, and
the node would start rejecting its own blocks. If the node's clock is
behind the network by more than `max_future_drift`, peers still reject
its blocks under §8.3 — a node with a badly wrong clock should not
produce blocks.

Mining is continuous: the block rate is governed by difficulty, not by
the miner. A miner that paused between blocks would make the network
look slower than it is, and the adjustment would drive difficulty down to
`min_difficulty`.

### 8.7 Known limitations

- **Stale mining is not interrupted.** A miner that receives a new tip
  while working on a block finishes the stale block before starting on
  the new tip; the result is rejected. At equilibrium difficulty this
  wastes up to one block interval per competing block.
- **The orphan pool is unbounded.** Branch validity (§8.5) is only checked
  once a branch connects to the chain, so blocks that never connect are
  held indefinitely. A size limit with eviction is required before this
  network faces untrusted peers.
- **Time-warp attacks** beyond what the median and the per-epoch clamp
  prevent are not addressed.
- **Changing any rule in this section is a hard fork.** Chains and
  databases built under earlier rules are not valid under later ones.
