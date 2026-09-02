# ForgeChain

[![CI](https://github.com/quaslir/forgechain/actions/workflows/ci.yml/badge.svg)](https://github.com/quaslir/forgechain/actions/workflows/ci.yml)

A blockchain built from first principles in C++ — no frameworks, no shortcuts. 
The goal isn't to reinvent Bitcoin, it's to actually understand how distributed 
consensus works by implementing every layer by hand: block structure, 
cryptographic signing, Proof-of-Work mining, peer-to-peer networking, and 
chain synchronization across multiple independent nodes.

## Why

Most blockchain explanations stop at "it's a chain of hashed blocks." 
This project goes further — into how nodes actually agree on a shared history 
without trusting each other, how forks get resolved, and what breaks when 
the network isn't perfect (latency, partitions, malicious peers).

## What's implemented

- [x] Block structure & hash-linked chain validation
- [x] ECDSA transaction signing & verification
- [x] Proof-of-Work mining with difficulty retargeting
- [x] P2P networking (handshake, block/tx propagation, gossip)
- [x] Multi-node synchronization & fork resolution (heaviest chain rule)
- [x] Mempool for pending transactions
- [x] Coinbase transactions & configurable mining rewards
- [x] Interactive node CLI (`balance`/`height`/`peers`) via `Orchestrator`
- [x] Standalone `wallet` (keypair generation/persistence, `send`/`balance`/`height`/`peers`)
- [x] RPC query channel, separate from the P2P protocol, connecting `wallet` to a node
- [x] Optional persistent storage based on SQLite for saving the chain and balances.
- [x] Transaction fees and an eviction policy.
- [x] Optional API key support for RPC.

See `docs/cli-usage.md` for the full CLI reference (every flag and command
on both `forgechain` and `wallet`), `docs/protocol.md` for the wire
protocol, `docs/fork-resolution.md` for how forks and reorgs are handled,
and `docs/docker-harness.md` for running a real 4-node network in Docker.

## Status

Work in progress — built as a learning project, one layer at a time.
