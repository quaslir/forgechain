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
