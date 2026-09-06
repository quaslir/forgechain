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
| `--mine-every` | `-m` | `<SECONDS>` | `0` (disabled) | Enable mining: attempt to mine a block every `N` seconds. Omit or pass `0` for a pure listening/relay node that never mines. |
| `--reward-address` | `-r` | `<ADDRESS>` | none | Address that receives the coinbase mining reward for every block this node mines. If unset, mined blocks contain no coinbase transaction. The node never holds a private key for this address -- generate one separately with `wallet` and pass its printed address here. |
| `--rpc-port` | `-R` | `<PORT>` | `0` (disabled) | Enable the RPC query server on this port (see §3). Separate from `--port`/the P2P port -- a client connecting here is never treated as a P2P peer. |
| `--rpc-api-key` | `-K` | `<VALUE>` | none | Require this value as the first token on every RPC command (see §3). If unset, the RPC server is unauthenticated -- anyone who can reach the RPC port can issue any command. Can also be set/changed after startup via the interactive `set secret-key <value>` command, without restarting the node. |
| `--db-path` | `-d` | `<PATH>` | none (disabled) | Enable persistent storage: the node's chain and account balances are saved to a SQLite database at this path on graceful shutdown, and restored from it on the next startup. If unset, the node runs entirely in memory and starts fresh every time. Persistence only happens on a clean stop (`quit`/`exit`/Ctrl+C) -- state is not saved continuously, so an unclean exit (a crash, `kill -9`) loses everything since the last clean stop. |

### Shutdown

`Ctrl+C` (SIGINT) triggers a graceful shutdown: mining and RPC threads are
stopped and joined, all peer connections are closed, and -- if `--db-path`
was given -- the current chain and balances are saved to disk, before the
process exits.

### Interactive commands (stdin)

Once running, the node reads commands from stdin on its own thread,
independent of mining/networking:

| Command | Description |
|---|---|
| `balance <address>` | Print the current `Ledger` balance for `<address>`, or `unknown address` if it has none. |
| `height` | Print the current chain height. |
| `peers` | List currently connected P2P peers, one per line, as `host:port` followed by direction: `(out)` for a connection this node dialed, `(in)` for one it accepted. Prints `(no peers connected)` if there are none. |
| `addrbook` | List every peer address this node knows, whether or not it is currently connected. Each entry shows `host:port` and its state: `verified` -- a successful outbound dial has confirmed the address is reachable, so it may be gossiped to other peers -- or `unverified (N fails)`, meaning the address is known but not yet confirmed, with its failed-dial count. Prints `(address book is empty)` if there are none. |
| `mempool` | List pending transactions in the mempool, each with sender, recipient, amount, and fee. Prints `(mempool is empty)` if there are none. |
| `status` | Print node configuration: P2P port, RPC state, mining interval, and reward address. |
| `connect <host> <port>` | Dial a peer immediately, without waiting for the connection loop. Returns within a few seconds even if the host is unreachable or silently drops packets. |
| `set reward-address <address>` | Set or change the address that receives the coinbase reward for blocks this node mines. |
| `set secret-key <value>` | Set or change the RPC auth token at runtime, without restarting the node. Takes effect immediately for all subsequent RPC commands; the previous token (if any) stops working right away. Errors if RPC isn't enabled (`--rpc-port` wasn't given). |
| `help` | Print the command list. |
| `quit` / `exit` | Shut down the node cleanly (saves to disk first if `--db-path` was given). |

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
| `send <address> <amount> [fee]` | Build, sign, and submit a transaction sending `<amount>` from this wallet's address to `<address>`, with an optional `<fee>` (defaults to `0` if omitted). Prints `sent` on success, `rejected by node` if the node's RPC server accepted the connection but rejected the transaction (e.g. malformed), or `network error` if the connection itself failed. |
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
| Submit transaction | `SUBMITTX <hex-encoded serialized transaction>\n` | `OK\n` if accepted for processing, `ERROR_EMPTY_PAYLOAD\n` / `ERROR_INVALID_HEX\n` / `ERROR_INVALID_PAYLOAD\n` on malformed input |
| Chain height | `HEIGHT\n` | `<height>\n` |
| Peer count | `PEERS\n` | `<count>\n` |
| Anything else | -- | `ERROR_UNKNOWN_COMMAND\n` |

(When a token is configured, prepend `<token> ` to any of the request lines
above.)

`PEERS` over RPC returns a bare count, while the node's interactive `peers`
command lists the peers themselves with addresses and direction. The two are
deliberately different: RPC is a machine interface with a fixed one-line
response shape, the interactive command is for operators.

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
<path>` turns on persistence: the node's blocks and account balances are
written to a SQLite database at that path, and read back to restore state
the next time the node starts with the same `--db-path`.

Persistence is snapshot-based, not continuous: state is written to disk
once, when the node shuts down cleanly (`quit`, `exit`, or Ctrl+C), not
after every mined block or transaction. This keeps the implementation
simple and avoids touching disk on every state change, but it means an
unclean exit -- a crash, `kill -9`, a power loss -- loses any blocks or
balance changes since the last clean shutdown. The rest of the network is
unaffected either way: a node that loses local state can always catch back
up from its peers over the normal P2P sync path, the same as a node
starting completely fresh.

Two nodes should never be pointed at the same `--db-path` file
simultaneously; each node's database is private to that single process.

## 5. Example: two nodes and a wallet

```
# Node A: listens on 8000, RPC on 8090, mines every 20s, pays itself
./forgechain --port 8000 --rpc-port 8090 --mine-every 20 \
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
./forgechain --port 8000 --rpc-port 8090 --mine-every 20 \
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
# First run: mines a few blocks, then shuts down cleanly.
./forgechain --port 8000 --mine-every 5 --db-path node.db --reward-address <address>
>>> height
3
>>> quit

# Second run, same --db-path: chain height picks up where it left off.
./forgechain --port 8000 --mine-every 5 --db-path node.db --reward-address <address>
>>> height
3
```
