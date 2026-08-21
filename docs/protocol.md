# forgechain P2P Protocol

This document specifies the wire format used for communication between
forgechain nodes. It is the reference for implementing the networking
layer

All multi-byte integers are encoded **little-endian** unless stated
otherwise, matching the host byte order this project already assumes
elsewhere (the existing `Block`/`Transaction` serialization writes raw
struct bytes directly via `reinterpret_cast`, which is little-endian on
the x86-64/ARM64 platforms this project targets).

## 1. Message framing

Every message sent over a TCP connection has the same fixed-size header,
followed by a variable-length payload:

+-------------+-----------+------------------+------------------------+
| magic (4B) | cmd (1B) | payload_len (4B) | payload (payload_len B)|
+-------------+-----------+------------------+------------------------+

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

`payload_len` must be checked against a maximum (e.g. a few megabytes)
before allocating a receive buffer — an unvalidated, attacker-controlled
length field is a classic memory-exhaustion vector. The exact limit is
an implementation detail of `#20`, not fixed here, but it must exist.

## 2. Command codes

| Code | Name | Payload | Purpose |
|---|---|---|---|
| `0x00` | `VERSION` | §3.1 | Handshake — announce protocol version and chain height on connect. |
| `0x01` | `INV` | §3.2 | Announce that the sender has a new block or transaction (by hash only). |
| `0x02` | `GETDATA` | §3.2 | Request the full contents of a previously announced `INV` item. |
| `0x03` | `BLOCK` | §3.3 | A full block, sent in response to `GETDATA` or during sync. |
| `0x04` | `TX` | §3.4 | A full transaction, sent in response to `GETDATA`. |
| `0x05` | `GETBLOCKS` | §3.5 | Request all blocks after a given known block hash (sync). |
| `0x06` | `PING` | §3.6 | Liveness check. |
| `0x07` | `PONG` | §3.6 | Response to `PING`. |

Codes `0x08`–`0xFF` are reserved for future message types.

## 3. Payload formats

### 3.1 `VERSION`

Sent immediately after a TCP connection is established, before any other
message is processed (see §4 for the handshake sequence).

| Field | Size | Type | Description |
|---|---|---|---|
| `protocol_version` | 4 bytes | `uint32` | Version of this protocol spec the sender implements. Starts at `1`. |
| `chain_height` | 8 bytes | `uint64` | Height of the sender's current best chain (number of blocks, including genesis). |
| `timestamp` | 8 bytes | `uint64` | Sender's current Unix timestamp (seconds). |

Total: 20 bytes.

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
| `item_hash` | 32 bytes | raw bytes | The block's `hash_` or the transaction's identifying hash (`double_sha_256` of `Transaction::serialize()`). |

### 3.3 `BLOCK`

The full serialized block, so the receiver can independently validate
and store it — not just the header, but every transaction inside.

| Field | Size | Type | Description |
|---|---|---|---|
| `version` | 4 bytes | `uint32` | Matches `Block::version_`. |
| `prev_hash` | 32 bytes | raw bytes | Matches `Block::prev_hash_`. |
| `merkle_root` | 32 bytes | raw bytes | Matches `Block::merkle_root_`. |
| `timestamp` | 8 bytes | `uint64` | Matches `Block::timestamp_`. |
| `difficulty` | 4 bytes | `uint32` | Matches `Block::difficulty_`. |
| `nonce` | 4 bytes | `uint32` | Matches `Block::nonce_`. |
| `tx_count` | 4 bytes | `uint32` | Number of transactions that follow. |
| transactions | variable | array | `tx_count` entries, each in `TX` payload format (§3.4), back to back. |

Note: this is a superset of what `Block::serialize()` currently produces
for hashing purposes — the hashing serialization only needs to be
internally consistent, but the wire format needs to be fully
**parseable**, so each transaction's fields must be individually
length-delimited (see §3.4) rather than just concatenated raw, which is
sufficient for hashing but not for unambiguous decoding.

### 3.4 `TX`

| Field | Size | Type | Description |
|---|---|---|---|
| `sender_len` | 4 bytes | `uint32` | Byte length of `sender` string. |
| `sender` | `sender_len` bytes | UTF-8 string | `Transaction::sender_`. |
| `recipient_len` | 4 bytes | `uint32` | Byte length of `recipient` string. |
| `recipient` | `recipient_len` bytes | UTF-8 string | `Transaction::recipient_`. |
| `amount` | 8 bytes | `uint64` | `Transaction::amount_`. |
| `signature_len` | 4 bytes | `uint32` | Byte length of `signature`. |
| `signature` | `signature_len` bytes | raw bytes | `Transaction::signature_` (DER-encoded ECDSA signature, variable length — see `crypto::sign`). |

This length-prefixed encoding is deliberately different from
`Transaction::serialize()` (used only for signing/hashing, where fields
are concatenated without delimiters). Wire encoding must be
unambiguously parseable — e.g. distinguishing `sender="ab", recipient="c"`
from `sender="a", recipient="bc"` — which concatenation alone cannot
guarantee (see the note already on file about this in
`Transaction::serialize()`'s known limitations).

### 3.5 `GETBLOCKS`

| Field | Size | Type | Description |
|---|---|---|---|
| `last_known_hash` | 32 bytes | raw bytes | Hash of the most recent block the requester already has. The responder replies with `INV` messages for everything after it, up to its own chain tip. |

### 3.6 `PING` / `PONG`

| Field | Size | Type | Description |
|---|---|---|---|
| `nonce` | 8 bytes | `uint64` | Arbitrary value chosen by the sender of `PING`; echoed back unchanged in the matching `PONG`, so the sender can match responses to requests and measure round-trip time. |

## 4. Handshake sequence

When node A connects to node B:

A B
|--------- VERSION -------------->| A announces its protocol version + chain height
|<-------- VERSION ----------------| B replies with its own
| |
| (both sides now know whether |
| they're on a compatible |
| protocol version, and who's |
| ahead in chain height) |
| |
|<-------- GETBLOCKS --------------| if B is behind A, B requests sync
|--------- INV -------------------->| A announces the blocks B is missing
|<-------- GETDATA ----------------| B requests the full blocks it wants
|--------- BLOCK ------------------->| A sends them, one per message

Rules:

- No message other than `VERSION` is processed until both sides have
  exchanged `VERSION`. A node receiving any other message type first
  should close the connection.
- If `protocol_version` values are incompatible (exact compatibility
  policy — e.g. exact match vs. minimum supported version — is an
  implementation decision for `#21`, not fixed here), the connection is
  closed after the version exchange rather than left half-negotiated.
- Whichever side has the lower `chain_height` is expected to initiate
  `GETBLOCKS`; the side with the higher height simply waits to see if a
  sync request arrives.

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
4. The receiving peer validates the data (PoW target, signature,
   balance, chain linkage as appropriate) before accepting it and
   re-announcing it via `INV` to its *other* peers (excluding the one it
   received it from) — this is what makes propagation reach the whole
   network, not just the direct neighbor.

A node must not re-broadcast the same `INV` for an item it has already
announced recently, to avoid redundant gossip storms — the exact
de-duplication window/mechanism is an implementation detail of `#22`.

## 6. Design notes and open decisions

These are intentionally left for the implementation issues rather than
fixed here, since they depend on details that only become clear while
building `#20`–`#24`:

- **Connection model**: one TCP connection per peer, bidirectional, or
  separate inbound/outbound connections? This document assumes a single
  bidirectional connection per peer pair.
- **Peer identification**: peers are currently identified by
  `IP:port` only; no persistent node identity (e.g. a public key) is
  defined yet. Adding one would let a node's identity survive an IP
  change, but is out of scope for the initial implementation.
- **Timeouts and reconnection**: not specified here. `PING`/`PONG` exists
  to support a liveness/timeout policy, but the actual timeout values
  and reconnection backoff strategy belong in `#20`.
- **Maximum message size**: mentioned in §1 as a required check, exact
  value left to `#20`.

## 7. Fork resolution and reorganization

When a node receives a valid `BLOCK` whose `prev_hash_` does not match its
current chain tip, this is not necessarily invalid data -- it may be the
start of a competing branch (fork). This section describes how a node
detects, evaluates, and (if warranted) switches to such a branch. The full
rationale for the weight metric is in `docs/fork-resolution.md`; this
section covers how it fits into the message-handling flow described in
§5 above.

### 7.1 Fork detection

On receiving a `BLOCK`, a node classifies it into exactly one of three
outcomes (`Blockchain::classify_new_block`):

- **Invalid** -- the block's declared hash does not match a fresh
  `compute_hash()` over its own fields. The content has been tampered
  with or is forged. The block is dropped immediately and never
  considered for propagation or fork resolution, regardless of what its
  `prev_hash_` claims.
- **Valid** -- the block's hash is authentic and `prev_hash_` matches the
  current chain tip. It is appended to the chain and re-announced via
  `INV` as in §5.
- **ForkCandidate** -- the block's hash is authentic, but `prev_hash_`
  points at an earlier block than the current tip. This is a legitimate
  competing branch, not garbage; it is *not* re-announced immediately
  (the node hasn't accepted it into its own chain yet), and is instead
  held for evaluation as described next.

Order matters: content authenticity is always checked before the
prev_hash_ comparison, so a forged block can never be misclassified as a
fork candidate just because it happens to carry an unrelated
`prev_hash_`.

### 7.2 Orphan pool

A `ForkCandidate` block is stored in a per-node orphan pool, keyed by its
own hash (`OrphanPool::add_orphan`), rather than discarded. This allows a
multi-block competing branch to be assembled incrementally as its blocks
arrive out of order or across multiple `BLOCK` messages, without requiring
all of them to arrive before any progress can be evaluated.

### 7.3 Locating the common ancestor

After a block is added to the orphan pool, the node attempts to trace a
path from that block backward through `prev_hash_` links -- through other
orphan-pool entries if necessary -- until it reconnects with a block
already present in the node's own chain (`build_fork_chain`). This
reconnection point is the *common ancestor*: where the competing branch
diverges from the node's current history.

This walk is capped at **`kMaxForkDepth` (100 blocks)** -- if no
connection to the main chain is found within that many hops, the branch
is treated as still-incomplete (more blocks may arrive later) rather than
immediately retried on every subsequent block. This cap exists because an
arbitrarily deep claimed fork (e.g. branching from hundreds of blocks
ago) is far more likely to indicate an attack or a wildly stale/malicious
peer than ordinary network latency -- legitimate forks in this network
are expected to be shallow, since block propagation (§5) is fast relative
to block production. A fork this deep is rejected outright: `nullopt` is
returned, the node's chain is left untouched, and no further work is
attempted on that branch until it either reaches the cap again with a
different/updated tip or is superseded by unrelated activity.

### 7.4 Weight comparison

Once a full path to a common ancestor is found, the node compares the
total accumulated work of the competing branch against its own chain's
tip (`is_fork_heavier`):

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

This is why the common ancestor block itself is carried alongside the
branch (rather than the branch's blocks being compared in isolation) --
orphan-pool blocks never pass through the normal chain-append path and so
never accumulate a meaningful `cumulative_work_` of their own; the
ancestor's already-correct value from the main chain is the necessary
starting point for an apples-to-apples comparison.

### 7.5 Reorganization procedure

If the competing branch is heavier, the node performs a reorganization
(`Blockchain::reorganize_to`):

1. Locate the common ancestor's height in the current chain.
2. Discard every block after that height (the losing branch).
3. Append the competing branch's blocks on top, in order, exactly as if
   each had arrived individually via normal block acceptance (§7.1) --
   this recomputes `cumulative_work_` correctly for each newly-applied
   block.

The blocks discarded in step 2 are returned to the caller so their
transactions can be handled -- see §7.6.

### 7.6 Mempool reinsertion

Transactions that were only present in the discarded (losing) blocks are
not simply lost: each is re-inserted into the node's mempool, so it can be
mined again in a future block, **unless** that same transaction (by hash)
is also present somewhere in the newly-adopted branch -- which can happen
if the transaction had independently propagated to and been mined by
whichever peer produced the winning branch. Re-inserting a transaction
that's already confirmed in the adopted chain would risk it being mined a
second time, effectively double-spending the same funds.

### 7.7 Propagation after a reorg

Once a reorganization completes, the newly-adopted branch's blocks are
announced via `INV` (§5) to the node's peers, one per block in the branch
(oldest first) -- not just the new tip. This ensures peers that don't
already share any of the intermediate blocks can still request and adopt
each one in turn via ordinary `GETDATA`/`BLOCK` exchange, rather than
receiving only the final tip and being unable to link it back to their
own chain.
