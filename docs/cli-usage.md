# CLI Reference: `forgechain` and `wallet`

This document is the reference for running a forgechain node and the
companion wallet: every startup flag, every interactive command, and the
plain-text RPC protocol that connects them.

## 1. `forgechain` (the node)

### Startup flags

| Flag | Alias | Argument | Default | Description |
|---|---|---|---|---|
| `--port` | `-p` | `<PORT>` | `8000` | P2P listen port. The node accepts inbound peer connections here. |
| `--connect` | `-c` | `<HOST> <PORT>` | none | Add a bootstrap peer. Repeatable -- pass `--connect` multiple times to seed several peers. Addresses go into the node's address book at startup and are dialed by the connection loop, so a peer that is temporarily unreachable is retried with exponential backoff rather than given up on. A peer that never answers is never fatal; the node still starts and listens. |
| `--bootstrap-file` | `-b` | `<PATH>` | none | Load additional peer addresses from a file (one `host:port` per line), in addition to any `--connect` flags. If the file doesn't exist or contains no valid entries, this is a warning, not a crash. |
| `--mine` | `-m` | none | off | Enable mining. The node mines continuously, one block after another, on its own thread; how often blocks actually appear is governed by the network's difficulty (see §5), not by the node. Omit for a pure listening/relay node that never mines. A mining node keeps one CPU core fully busy. |
| `--reward-address` | `-r` | `<ADDRESS>` | none | Address that receives the coinbase mining reward for every block this node mines. If unset, mined blocks contain no coinbase transaction. The node never holds a private key for this address -- generate one separately with `wallet` and pass its printed address here. |
| `--rpc-port` | `-R` | `<PORT>` | `0` (disabled) | Enable the RPC query server on this port (see §3). Separate from `--port`/the P2P port -- a client connecting here is never treated as a P2P peer. |
| `--rpc-api-key` | `-K` | `<VALUE>` | none | Require this value as the first token on every RPC command (see §3). If unset, the RPC server is unauthenticated -- anyone who can reach the RPC port can issue any command. Can also be set/changed after startup via the interactive `set secret-key <value>` command, without restarting the node. |
| `--db-path` | `-d` | `<PATH>` | none (disabled) | Enable persistent storage (see §4): every block the node accepts is written to a SQLite database at this path as it is accepted, and the chain is restored from it on the next startup. If unset, the node runs entirely in memory and starts fresh every time. |

### Shutdown

`Ctrl+C` (SIGINT) triggers a graceful shutdown: mining and RPC threads are
stopped and joined, and all peer connections are closed before the process
exits. A mining node abandons the block it is working on: the mining thread
checks for shutdown every 65,536 hashes, so it stops within a fraction of a
second even at high difficulty. There is no save step on shutdown -- with `--db-path`, the database is
already up to date with every accepted block, so a clean stop and an unclean
one (a crash, `kill -9`) leave the same data on disk.

### Interactive commands (stdin)

Once running, the node reads commands from stdin on its own thread,
independent of mining/networking:

| Command | Description |
|---|---|
| `balance <address>` | Print the current `Ledger` balance for `<address>`, or `unknown address` if it has none. |
| `ledger` | Print every account the `Ledger` knows, with its balance. The first line is the account count (`N account(s):`), followed by one `address : amount` line per account. Order is unspecified and may differ between calls. Prints `(ledger is empty)` if no account has a balance yet. |
| `height` | Print the current chain height. |
| `block <height>` | Print the full contents of the block at `<height>`: its hash, the hash of its parent, merkle root, timestamp (raw and as UTC), difficulty, nonce, its own and the chain's cumulative work, and every transaction in it with hash, sender, recipient, amount, fee and nonce. Heights start at `0` (genesis); a height at or above the current chain height prints a message instead. Comparing the hash at the same height on two nodes is the quickest way to tell whether they are on the same chain. |
| `peers` | List currently connected P2P peers, one per line, as `host:port` followed by direction: `(out)` for a connection this node dialed, `(in)` for one it accepted. Prints `(no peers connected)` if there are none. |
| `addrbook` | List every peer address this node knows, whether or not it is currently connected. Each entry shows `host:port` and its state: `verified` -- a successful outbound dial has confirmed the address is reachable, so it may be gossiped to other peers -- or `unverified (N fails)`, meaning the address is known but not yet confirmed, with its failed-dial count. Prints `(address book is empty)` if there are none. |
| `mempool` | List pending transactions in the mempool, each with sender, recipient, amount, and fee. Prints `(mempool is empty)` if there are none. |
| `status` | Print node configuration and state: P2P port, RPC state, whether mining is on, the reward address, and the difficulty (in bits) that consensus requires for the next block. The difficulty line is shown on every node, mining or not -- it is a property of the chain, not of the miner. |
| `connect <host> <port>` | Dial a peer immediately, without waiting for the connection loop. Returns within a few seconds even if the host is unreachable or silently drops packets. |
| `set reward-address <address>` | Set or change the address that receives the coinbase reward for blocks this node mines. |
| `set secret-key <value>` | Set or change the RPC auth token at runtime, without restarting the node. Takes effect immediately for all subsequent RPC commands; the previous token (if any) stops working right away. Errors if RPC isn't enabled (`--rpc-port` wasn't given). |
| `help` | Print the command list. |
| `quit` / `exit` | Shut down the node cleanly. |

### Peer discovery

A node learns peer addresses three ways: from `--connect` and
`--bootstrap-file` at startup, from `PEERS` messages gossiped by peers it is
connected to, and from peers that dial in to it.

Only addresses a node has itself confirmed by a successful outbound dial are
passed on to others. An address learned any other way is held as unverified
until the connection loop manages to dial it. This is what keeps a node
behind NAT -- reachable outbound, but not dialable from outside -- from being
advertised network-wide and dialed forever by everyone who hears about it.

Because verification requires an outbound dial, a node whose peers are all
inbound still confirms their addresses: the connection loop probes each one
even while the connection is live, and the probe result decides whether the
address becomes gossipable. Confirmed addresses are then sent to every
connected peer on a fixed interval, so a hub node with no outbound
connections of its own still introduces its peers to each other.

Use `addrbook` to watch this happen: an address appears unverified first,
and flips to verified once a dial to it succeeds.

### Chain sync

A node picks up blocks from its peers in three ways:

- **New blocks** are announced by the peer that accepted them and fetched
  immediately. This only covers blocks that appear while the connection is
  up.
- **On connect**, if the other side's chain is taller, the node asks it to
  catch it up.
- **Periodically**, the node asks one connected taller peer -- a different
  one each time, round-robin. This recovers anything missed on a live
  connection (a lost announcement, a stalled peer) without reconnecting.

A request does not ask for a height. The node sends a list of hashes of
blocks it has -- every one of the last ten, then at doubling intervals
further back, ending at the genesis block -- and the peer replies with
everything after the most recent block the two of them share. This is what
lets nodes reconcile chains that diverged long ago: a height means nothing
if each node holds a different block at it, but a matching hash proves the
two chains are identical up to that point.

A peer answers any single request with at most 2000 blocks. A node further
behind than that catches up over several rounds; watch `height` climb to
see it progress. When it is already in sync, the request gets an empty
answer and generates no traffic.

A node that has been offline while the network moved on -- restarted from
`--db-path`, or on a laptop that was closed -- rejoins by asking whichever
peer it connects to, and jumps to the network's chain in one step. The
`SYNC` line in the log reports how many blocks arrived and the height
afterwards. Its own blocks mined on the old chain are discarded in the
process; that is the network agreeing on the heavier chain, not an error.

Two things to know when watching this happen:

- Right after a catch-up, a mining node drops the block it was working on
  and starts on the new tip; it does not log a `REJECTED` line for it. A
  `block REJECTED (stale or invalid)` line now means a real race: another
  block became the tip in the instant between the miner's last check and
  its submission. Harmless, and rare.
- `block <height>` prints a block's hash, so comparing the same height on
  two nodes tells you in one command whether they agree.

## 2. `wallet`

### Startup flags

| Flag | Alias | Argument | Required | Description |
|---|---|---|---|---|
| `--keyfile` | `-k` | `<PATH>` | yes | Path to the wallet's keypair file. If the file doesn't exist, a fresh keypair is generated and saved there; if it exists, it's loaded. A malformed keyfile (wrong number of lines, invalid hex) throws and the wallet refuses to start rather than run with a corrupted key. |
| `--connect` | `-c` | `<HOST> <PORT>` | yes | Address of the node's **RPC port** (the value passed to that node's `--rpc-port`, *not* its `--port`). All wallet commands talk to the node exclusively over this RPC channel. |
| `--rpc-api-key` | `-K` | `<VALUE>` | no | Auth token to send with every RPC command, if the target node has one configured via its own `--rpc-api-key`/`set secret-key`. Omit if the node is unauthenticated. Can also be set/changed after startup via the interactive `set rpc-api-key <value>` command. |

The wallet's own address (derived from the generated/loaded public key) is
printed once at startup.

### Keyfile format

Plain text, two lines: hex-encoded private key on the first line,
hex-encoded public key on the second.

### Interactive commands (stdin)

| Command | Description |
|---|---|
| `send <address> <amount> [fee]` | Build, sign, and submit a transaction sending `<amount>` from this wallet's address to `<address>`, with an optional `<fee>` (defaults to `0` if omitted). The transaction's nonce (see §6) is fetched from the node first, so a failed or unreachable node makes the whole command fail rather than send a transaction with a guessed nonce. Prints `sent` on success, `rejected by node` if the node's RPC server accepted the connection but rejected the transaction (e.g. malformed), or `network error` if the connection itself failed -- including when the nonce could not be fetched. |
| `balance` | Query and print this wallet's own balance via RPC. Prints `UNKNOWN` if the node has no record of this address, or `network error` on a connection failure. |
| `height` | Query and print the connected node's current chain height. |
| `peers` | Query and print the connected node's current P2P peer count. This is a bare number over RPC -- to see the peers themselves, use the node's own `peers` command on its stdin. |
| `connect <host> <port>` | Change which node's RPC port this wallet talks to, without restarting. The configured auth token is kept across the switch. |
| `set rpc-api-key <value>` | Set or change the RPC auth token sent with every subsequent command. Persists across `connect` (switching which node's RPC port to talk to does not clear the token). |
| `help` | Print the command list. |
| `quit` / `exit` | Exit the wallet. |

## 3. RPC protocol

A minimal plain-text protocol, deliberately separate from the P2P wire
protocol (see `protocol.md`). One command per TCP connection: connect, send
one newline-terminated line, read one newline-terminated line back,
disconnect. No handshake, no `VersionInfo` exchange -- an RPC client is never
registered as a P2P peer and never appears in `peers` P2P peer counts.

### Authentication

If the node has an RPC token configured (via `--rpc-api-key` at startup, or
`set secret-key <value>` while running), every command must be prefixed with
that token as its own space-separated word:

```
<token> GETBALANCE <address>\n
```

A missing or incorrect token gets `ERROR unauthorized\n` -- the same
response regardless of whether the command that followed would otherwise
have been valid, so a client probing for the right token learns nothing
about command validity from the response.

If no token is configured, commands are sent exactly as documented below,
with no prefix -- this is the default. The token can be changed at runtime
without restarting the node; the previous token stops working the moment a
new one is set.

| Command | Request | Response |
|---|---|---|
| Get balance | `GETBALANCE <address>\n` | `<amount>\n` if known, `UNKNOWN\n` if not, `ERROR_EMPTY_ADDRESS\n` if no address was given |
| Get next nonce | `GETNONCE <address>\n` | `<nonce>\n`, or `ERROR_EMPTY_ADDRESS\n` if no address was given |
| Submit transaction | `SUBMITTX <hex-encoded serialized transaction>\n` | `OK\n` if accepted for processing, `ERROR_EMPTY_PAYLOAD\n` / `ERROR_INVALID_HEX\n` / `ERROR_INVALID_PAYLOAD\n` on malformed input |
| Chain height | `HEIGHT\n` | `<height>\n` |
| Peer count | `PEERS\n` | `<count>\n` |
| Anything else | -- | `ERROR_UNKNOWN_COMMAND\n` |

(When a token is configured, prepend `<token> ` to any of the request lines
above.)

`GETNONCE` reports the nonce the address's next transaction must carry (see
§6). Unlike `GETBALANCE` there is no `UNKNOWN`: an address nobody has heard
of is simply at `0`. A wallet calls this before signing, so `SUBMITTX` is
normally preceded by a `GETNONCE` on its own connection.

`PEERS` over RPC returns a bare count, while the node's interactive `peers`
command lists the peers themselves with addresses and direction. The two are
deliberately different: RPC is a machine interface with a fixed one-line
response shape, the interactive command is for operators.

There is no RPC counterpart to the interactive `ledger` command; over RPC,
balances can only be queried one address at a time with `GETBALANCE`.

`OK` on `SUBMITTX` means the transaction was handed to the node for
processing -- it does not guarantee the transaction is in the mempool or
will ever be mined; `Node::submit_transaction` has no return value to
confirm that. A rejected transaction (bad signature, insufficient balance,
etc.) still gets `OK` from the RPC layer, since that check happens
downstream, inside `Mempool`/`Ledger`.

Lines longer than 4096 bytes are rejected with `ERROR` and the connection is
closed.

## 4. Persistent storage

By default a node keeps its entire chain and ledger in memory and starts
from a fresh genesis block every time it's launched. Passing `--db-path
<path>` turns on persistence: the node's blocks are written to a SQLite
database at that path, and read back to restore state the next time the
node starts with the same `--db-path`.

Persistence is incremental. Each block is written the moment the node
accepts it -- whether the node mined it or received it from a peer -- not
in a batch at shutdown. When a longer competing chain replaces the tail of
the current one (a reorg), the replaced blocks are deleted and the new ones
written in a single transaction, so the database never holds a half-applied
reorg. The practical consequence: killing the node at any point, including
`kill -9` or a power loss, loses at most the block that was being accepted
at that instant.

Only blocks are stored. Account balances are not saved separately; on
startup the node replays every stored block in order to rebuild them. This
keeps the database from ever disagreeing with itself, at the cost of
startup time that grows with the length of the chain.

If the stored chain can't be replayed -- a block is missing, or a block no
longer applies cleanly on top of the ones before it -- the node refuses to
start with a `storage corrupted: ...` error naming the offending height,
rather than run on an inconsistent chain. Deleting the database file and
restarting recovers: the node starts from genesis and resyncs from its
peers over the normal P2P path.

Mempool contents are not persisted. Pending transactions that were not yet
mined are gone after a restart.

Two nodes should never be pointed at the same `--db-path` file
simultaneously; each node's database is private to that single process.

A database is only valid under the consensus rules it was built with.
Databases created before difficulty adjustment (§5) was introduced hold
blocks whose difficulty and timestamps the current rules do not require
of them; the node still loads such a file, but peers running the current
version will reject its chain. Delete old database files when upgrading
across that change.

## 5. Difficulty adjustment

Difficulty is not configured on the node. Consensus derives the difficulty
every block must carry from the chain itself, and every node enforces it:
a block with any other difficulty is rejected, whether it extends the tip
or arrives as part of a competing branch. The exact rules are in
`protocol.md` §8; this section describes what an operator sees.

Difficulty is recalculated once every 20 blocks, aiming for one block
every 10 seconds across the whole network. If the previous 20 blocks came
in faster than that, difficulty goes up; slower, it goes down. A single
adjustment moves by at most 2 bits (a factor of 4 in expected work), so
reaching equilibrium from a far-off starting point takes several rounds.

What this looks like in practice:

- **A fresh network starts fast.** The first blocks are mined at the
  initial difficulty, which on typical hardware takes milliseconds.
  Difficulty then climbs by 2 bits per round until block times approach
  10 seconds, and settles there. Watch the `difficulty` line in `status`
  and `height` to follow it.
- **More miners, same block rate.** Adding a mining node makes blocks come
  faster for a while; the next adjustment raises difficulty to compensate.
  Removing miners does the reverse.
- **All nodes agree.** Difficulty is a function of the chain, so nodes on
  the same chain always report the same `difficulty` in `status`. Two
  nodes showing different values at the same height are on different
  branches.
- **Difficulty moves in whole bits.** If the network's real hash rate sits
  between two bit values, difficulty can alternate between them from one
  round to the next. Average block time still stays close to the target.

Block timestamps are also checked: a block must be later than the median
of the previous 11 blocks, and no more than 2 minutes ahead of the
receiving node's clock. A node whose system clock is badly off will see
its peers' fresh blocks rejected (clock behind) or have its own mined
blocks rejected by peers (clock ahead). Keep node clocks synchronized
(NTP) -- a few seconds of skew is harmless, minutes is not.

## 6. Transaction nonces

Every transaction carries a nonce: the number of transactions its sender had
already made. The first transaction from an address uses `0`, the next `1`,
and so on. A transaction is only valid when its nonce matches the sender's
current count, which is what stops an old signed transaction from being
submitted again later to debit the sender a second time. It is also what
lets a sender pay the same recipient the same amount twice -- without a
nonce the two transactions would be byte-for-byte identical.

The wallet handles this: `send` asks the node for the address's next nonce
before signing. Nothing to configure, but two consequences are worth
knowing.

**One transaction at a time.** The count advances when a transaction is
*mined*, not when it is submitted. Sending twice in a row without waiting
for a block gives both transactions the same nonce, and the second is
rejected. Check `balance` and wait for it to reflect the first transfer
before sending again.

**Balance lags behind.** Right after `send`, `balance` still shows the old
amount: the transaction is in the mempool, not yet in a block. On a mining
node the amount changes within a block or two -- and on a node that also
mines to this address, the mining reward moves it up at the same time.

## 7. Example: two nodes and a wallet

```
# Node A: listens on 8000, RPC on 8090, mines, pays itself
./forgechain --port 8000 --rpc-port 8090 --mine \
  --reward-address <address-from-wallet-below>

# Node B: connects to A, no mining, RPC on 8091
./forgechain --port 8001 --connect 127.0.0.1 8000 --rpc-port 8091

# Wallet: generates/loads a key, talks to node A's RPC port
./wallet --keyfile keys.txt --connect 127.0.0.1 8090
>>> balance
100
>>> send <some-other-address> 25
sent
```

### Example: inspecting balances

```
# On node A's stdin, after a few blocks and the send above:
>>> ledger
2 account(s):
  <address-from-wallet> : 275
  <some-other-address> : 25
```

### Example: comparing two nodes' chains

```
# On each node, at a height both have passed:
>>> block 41000
height:     41000
hash:       7b3c...e91a
prev:       0f42...88d3
merkle:     a115...6c07
timestamp:  1757600441  (2025-09-11 14:20:41 UTC)
difficulty: 19 bits
nonce:      338211
work:       524288  (cumulative 21487681536)
version:    1
transactions: 1
  [0] 4d9e...b210
      COINBASE -> 62c918e0d9a01a414f9223e8d35f93371fcc85df29ecc1aba2407e11808c3094
      amount 50, fee 0  (coinbase)
```

Same hash on both nodes means the chains agree up to that height. Different
hashes mean they have diverged somewhere at or below it, and the next sync
round will settle it: whichever chain carries more work wins, and the other
node switches to it.

### Example: inspecting peer discovery

```
# Three nodes where B is a hub: A and C both dial B, nothing else.
./forgechain --port 8000 --connect 127.0.0.1 8001
./forgechain --port 8001
./forgechain --port 8002 --connect 127.0.0.1 8001

# On B, both links show as inbound:
>>> peers
127.0.0.1:8000 (in)
127.0.0.1:8002 (in)

# ...and both addresses start out unconfirmed:
>>> addrbook
127.0.0.1:8000 unverified (0 fails)
127.0.0.1:8002 unverified (0 fails)

# Once B has probed them, they flip to verified and become gossipable,
# and A and C learn about each other from B.
>>> addrbook
127.0.0.1:8000 verified
127.0.0.1:8002 verified
```

### Example: with an RPC token

```
# Node A, started with a token baked in from the start
./forgechain --port 8000 --rpc-port 8090 --mine \
  --reward-address <address> --rpc-api-key mysecret123

# Wallet, given the same token up front
./wallet --keyfile keys.txt --connect 127.0.0.1 8090 --rpc-api-key mysecret123
>>> height
5

# Or set/change the token on either side after the fact, without restarting:
# on the node's own stdin:
>>> set secret-key newsecret456
RPC secret key updated

# on the wallet's stdin, to match:
>>> set rpc-api-key newsecret456
RPC API key set
```

### Example: with persistent storage

```
# First run: mines for a while.
./forgechain --port 8000 --mine --db-path node.db --reward-address <address>
>>> height
3

# Kill it hard from another terminal -- no clean shutdown.
kill -9 <pid>

# Second run, same --db-path: chain height and balances pick up where they
# left off, and mining continues from there. (Run without --mine to see
# the restored height hold still.)
./forgechain --port 8000 --db-path node.db
>>> height
3
```

### Example: catching up after downtime

```
# Node A has been mining for a while and is at height 669.
./forgechain --port 8000 --mine --db-path node.db

# Node B starts from nothing and connects:
./forgechain --port 8001 --mine --connect 127.0.0.1 8000
[..] [PEER] connected to 127.0.0.1:8000
[..] [MINE] block ACCEPTED, height now 153      # its own short chain
[..] [SYNC] 668 block(s) received, adopted, height now 669

# From here both nodes stay within a block or two of each other.
>>> height
669
```

The 153 blocks B mined on its own are gone: A's chain carried more work.
Comparing `block 100` on both nodes now gives the same hash.

### Example: sending twice

```
>>> balance
500
>>> send <recipient> 100
sent
>>> send <recipient> 50
rejected by node          # same nonce as the first: still unmined

# after a block arrives:
>>> balance
400
>>> send <recipient> 50
sent
```

### Example: watching difficulty adjust

```
# A fresh three-node network, every node mining.
./forgechain --port 8000 --mine
./forgechain --port 8001 --connect 127.0.0.1 8000 --mine
./forgechain --port 8002 --connect 127.0.0.1 8000 --mine

# Right after start, blocks come in milliseconds apart:
>>> status
port: 8000
rpc: disabled
mining: active
reward address: none (mined blocks have no coinbase)
difficulty: 15 bits (next block)

# A few adjustment rounds later difficulty has climbed and block times
# are near 10 seconds. Every node reports the same value:
>>> status
...
difficulty: 23 bits (next block)
```

The exact equilibrium depends on the combined hash rate of the mining
nodes; 23 bits is only an illustration.
