# ForgeChain

[![CI](https://github.com/quaslir/forgechain/actions/workflows/ci.yml/badge.svg)](https://github.com/quaslir/forgechain/actions/workflows/ci.yml)

A Bitcoin-style blockchain written from first principles in C++20 — no
frameworks, no blockchain libraries. Every layer is implemented by hand:
block structure, ECDSA signing, Proof-of-Work with difficulty retargeting,
a POSIX-socket P2P protocol, fork resolution with chain reorganization, and
SQLite persistence.

## Demo

[![asciicast](https://asciinema.org/a/8JzLuEOLAbBdxFHe.svg)](https://asciinema.org/a/8JzLuEOLAbBdxFHe)

A network of four nodes mining, gossiping and agreeing on one history runs
with a single command (see [Quick start](#quick-start)).

## Why

Most blockchain explanations stop at "it's a chain of hashed blocks." This
project goes further: how nodes agree on a shared history without trusting
each other, how competing branches are resolved, and what breaks when the
network isn't perfect — latency, partitions, peers that lie about their
chain.

Deliberate constraints: the standard library plus OpenSSL and SQLite only.
Raw POSIX sockets instead of Boost.Asio, since the networking is the part
worth learning.

## Quick start

A four-node network in Docker:

```bash
docker compose up --build
```

Each node mines, relays blocks to its neighbours, and converges on the same
height. Details in [`docs/docker-harness.md`](docs/docker-harness.md).

Building locally:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/src/app/forgechain --port 8000 --mine --reward-address <ADDRESS>
```

Requires a C++20 compiler, CMake 3.20+, OpenSSL and SQLite3. Generate an
address with `./build/src/app/wallet`. Full flag and command reference:
[`docs/cli-usage.md`](docs/cli-usage.md).

## What's implemented

- Hash-linked blocks with Merkle roots and full validation
- ECDSA (secp256k1) transaction signing, address derivation, nonce-based
  replay protection
- Proof-of-Work mining, difficulty retargeting, median-time-past timestamp
  rules
- Mining interruption: a miner drops its block the moment the tip moves
- P2P over raw POSIX sockets: handshake, INV/GETDATA/BLOCK/TX propagation,
  peer gossip with address verification, PING/PONG heartbeat
- Catch-up sync via block locators
- Fork resolution by cumulative work, with ledger and mempool rollback on
  reorganization
- Mempool with fee-ordered selection, a size cap and fee-based eviction
- Coinbase transactions, mining rewards and fees
- SQLite persistence: blocks written on acceptance, reorgs applied
  atomically, state rebuilt on startup
- RPC channel with optional API key auth, used by the standalone `wallet`
- Interactive node CLI: `balance`, `height`, `block`, `peers`, `addrbook`,
  `mempool`, `ledger`, `status`

Tested with GoogleTest: <!-- TODO: fill in --> tests covering consensus
rules, fork resolution, persistence, and concurrent networking, run in CI
under AddressSanitizer and UBSan on every push.

## Documentation

| Document | What it covers |
| --- | --- |
| [`docs/protocol.md`](docs/protocol.md) | Wire format, message types, consensus rules |
| [`docs/fork-resolution.md`](docs/fork-resolution.md) | How competing branches are compared and adopted |
| [`docs/cli-usage.md`](docs/cli-usage.md) | Every flag and command on `forgechain` and `wallet` |
| [`docs/docker-harness.md`](docs/docker-harness.md) | Running a real multi-node network |

## Architecture

```
app/        Orchestrator (node process), wallet, RPC server, CLI
chain/      ChainManager — owns all chain state behind one lock boundary
core/       Block, Transaction, Blockchain, Mempool, Ledger, OrphanPool
consensus/  Proof of Work, difficulty retargeting, timestamp rules
network/    Node (transport only), Peer, TcpSocket, AddressBook, handshake
storage/    SQLite persistence behind RAII wrappers
crypto/     SHA-256, ECDSA, address derivation
```

`ChainManager` owns every piece of chain state and all the locking around
it; `Node` is pure transport and holds a reference to it. That split is
what makes the concurrency tractable.

## Known limitations

This is a learning project, not production software, and it is honest about
what it does not do. `docs/protocol.md` §8.8 lists the consensus-level
limitations in full; the main ones:

- No SPV, no UTXO model — an account/balance ledger instead
- A sender cannot have two transactions in flight (the nonce advances on
  mining, not on submission)
- The orphan pool is size-capped but admits blocks before their required
  difficulty is checked
- Time-warp attacks beyond the median-time-past and per-epoch clamp are not
  addressed
- No peer scoring or banning; a misbehaving peer is disconnected, not
  remembered

## License

MIT — see [LICENSE](LICENSE).
