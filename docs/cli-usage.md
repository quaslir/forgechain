# CLI Reference: `forgechain` and `wallet`

This document is the reference for running a forgechain node and the
companion wallet: every startup flag, every interactive command, and the
plain-text RPC protocol that connects them.

## 1. `forgechain` (the node)

### Startup flags

| Flag | Alias | Argument | Default | Description |
|---|---|---|---|---|
| `--port` | `-p` | `<PORT>` | `8000` | P2P listen port. The node accepts inbound peer connections here. |
| `--connect` | `-c` | `<HOST> <PORT>` | none | Connect to a bootstrap peer on startup. Repeatable -- pass `--connect` multiple times to dial several peers. A failed connect is logged as a warning and is not fatal; the node still starts and listens. |
| `--bootstrap-file` | `-b` | `<PATH>` | none | Load additional peer addresses from a file (one `host:port` per line) and connect to each, in addition to any `--connect` flags. If the file doesn't exist or contains no valid entries, this is a warning, not a crash. |
| `--mine-every` | `-m` | `<SECONDS>` | `0` (disabled) | Enable mining: attempt to mine a block every `N` seconds. Omit or pass `0` for a pure listening/relay node that never mines. |
| `--reward-address` | `-r` | `<ADDRESS>` | none | Address that receives the coinbase mining reward for every block this node mines. If unset, mined blocks contain no coinbase transaction. The node never holds a private key for this address -- generate one separately with `wallet` and pass its printed address here. |
| `--rpc-port` | `-R` | `<PORT>` | `0` (disabled) | Enable the RPC query server on this port (see §3). Separate from `--port`/the P2P port -- a client connecting here is never treated as a P2P peer. |

### Shutdown

`Ctrl+C` (SIGINT) triggers a graceful shutdown: mining and RPC threads are
stopped and joined, all peer connections are closed, before the process
exits.

### Interactive commands (stdin)

Once running, the node reads commands from stdin on its own thread,
independent of mining/networking:

| Command | Description |
|---|---|
| `balance <address>` | Print the current `Ledger` balance for `<address>`, or `unknown address` if it has none. |
| `height` | Print the current chain height. |
| `peers` | Print the number of currently connected P2P peers. |
| `quit` / `exit` | Shut down the node cleanly. |

## 2. `wallet`

### Startup flags

| Flag | Alias | Argument | Required | Description |
|---|---|---|---|---|
| `--keyfile` | `-k` | `<PATH>` | yes | Path to the wallet's keypair file. If the file doesn't exist, a fresh keypair is generated and saved there; if it exists, it's loaded. A malformed keyfile (wrong number of lines, invalid hex) throws and the wallet refuses to start rather than run with a corrupted key. |
| `--connect` | `-c` | `<HOST> <PORT>` | yes | Address of the node's **RPC port** (the value passed to that node's `--rpc-port`, *not* its `--port`). All wallet commands talk to the node exclusively over this RPC channel. |

The wallet's own address (derived from the generated/loaded public key) is
printed once at startup.

### Keyfile format

Plain text, two lines: hex-encoded private key on the first line,
hex-encoded public key on the second.

### Interactive commands (stdin)

| Command | Description |
|---|---|
| `send <address> <amount>` | Build, sign, and submit a transaction sending `<amount>` from this wallet's address to `<address>`. Prints `sent` on success, `rejected by node` if the node's RPC server accepted the connection but rejected the transaction (e.g. malformed), or `network error` if the connection itself failed. |
| `balance` | Query and print this wallet's own balance via RPC. Prints `UNKNOWN` if the node has no record of this address, or `network error` on a connection failure. |
| `height` | Query and print the connected node's current chain height. |
| `peers` | Query and print the connected node's current P2P peer count. |
| `quit` / `exit` | Exit the wallet. |

## 3. RPC protocol

A minimal plain-text protocol, deliberately separate from the P2P wire
protocol (see `protocol.md`). One command per TCP connection: connect, send
one newline-terminated line, read one newline-terminated line back,
disconnect. No handshake, no `VersionInfo` exchange -- an RPC client is never
registered as a P2P peer and never appears in `peers` P2P peer counts.

| Command | Request | Response |
|---|---|---|
| Get balance | `GETBALANCE <address>\n` | `<amount>\n` if known, `UNKNOWN\n` if not, `ERROR_EMPTY_ADDRESS\n` if no address was given |
| Submit transaction | `SUBMITTX <hex-encoded serialized transaction>\n` | `OK\n` if accepted for processing, `ERROR_EMPTY_PAYLOAD\n` / `ERROR_INVALID_HEX\n` / `ERROR_INVALID_PAYLOAD\n` on malformed input |
| Chain height | `HEIGHT\n` | `<height>\n` |
| Peer count | `PEERS\n` | `<count>\n` |
| Anything else | -- | `ERROR_UNKNOWN_COMMAND\n` |

`OK` on `SUBMITTX` means the transaction was handed to the node for
processing -- it does not guarantee the transaction is in the mempool or
will ever be mined; `Node::submit_transaction` has no return value to
confirm that. A rejected transaction (bad signature, insufficient balance,
etc.) still gets `OK` from the RPC layer, since that check happens
downstream, inside `Mempool`/`Ledger`.

Lines longer than 4096 bytes are rejected with `ERROR` and the connection is
closed.

## 4. Example: two nodes and a wallet

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
