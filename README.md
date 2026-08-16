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
- [ ] P2P networking (handshake, block/tx propagation, gossip)
- [ ] Multi-node synchronization & fork resolution (heaviest chain rule)
- [ ] Mempool for pending transactions

## Status

Work in progress — built as a learning project, one layer at a time.
