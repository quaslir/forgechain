# Fork Resolution: Chain Weight Metric

This document specifies how forgechain compares two competing chains (or
branches) and decides which one is canonical. It is the reference for
implementing Stage 6 fork resolution.

## 1. The problem

`Blockchain` is currently a simple linear sequence of blocks. When a node
receives a new block whose `prev_hash_` does not match its current tip, it
is looking at the start of a fork: a valid alternative history that
diverges from its own chain at some earlier point. A decentralized network
needs a deterministic rule for which of two (or more) valid chains every
honest node should converge on — otherwise different nodes could
permanently disagree about the state of the ledger.

## 2. Why block count alone is insufficient

The naive rule — "the chain with more blocks wins" — is not safe, because
forgechain's difficulty is not fixed. `consensus::retarget` adjusts
`difficulty_` up or down over time (see `src/consensus/ProofOfWork.cpp`),
so blocks at different points in a chain's history can require
different amounts of work to produce.

This makes a pure length comparison exploitable: an attacker (or just an
unlucky network partition) could produce a chain with *more* blocks but
*less* total computational work than the honest chain, by mining many
blocks at low difficulty. If nodes simply picked the longer chain, an
attacker with less real hashing power than the honest network could still
force a reorg by mining quickly at low difficulty rather than needing to
out-mine the network at the true difficulty level.

The chain that represents the most actual proof-of-work — the most total
effort spent finding valid hashes — is the one that should be treated as
canonical. This is the same reasoning Bitcoin uses ("heaviest chain," not
"longest chain," despite the common shorthand).

## 3. Chosen metric: cumulative work

### 3.1 How difficulty relates to work

forgechain's `difficulty_` field is the number of required leading zero
*bits* in a block's hash (see `consensus::meets_target`, which counts
leading zero bits and compares against `difficulty`). A hash is uniformly
distributed, so the probability that a randomly hashed nonce satisfies a
given `difficulty` is `1 / 2^difficulty`. The expected number of attempts
(hashes) needed to find a valid nonce — i.e. the *work* — is therefore
proportional to `2^difficulty`.

### 3.2 Formula

The work contributed by a single block is:

```
work(block) = 2 ^ block.difficulty_
```

The cumulative work of a chain is the sum of the work of every block in
it:

```
cumulative_work(chain) = sum( 2 ^ block.difficulty_  for block in chain )
```

Given two competing chains that share a common ancestor, the chain with
the higher `cumulative_work` is canonical. If both chains have identical
cumulative work (possible but exceedingly unlikely in practice), the
current tip is kept and the competing chain is discarded — ties do not
trigger a reorg, to avoid nodes flapping between two equally-heavy chains
as new blocks trickle in.

### 3.3 Worked example

Chain A: 5 blocks, each mined at `difficulty = 20`
`cumulative_work(A) = 5 * 2^20 = 5,242,880`

Chain B: 10 blocks, each mined at `difficulty = 8`
`cumulative_work(B) = 10 * 2^8 = 2,560`

Despite having fewer blocks, chain A represents roughly 2000x more actual
work and is the heavier — and therefore canonical — chain.

## 4. Representation and overflow

Cumulative work is stored as `uint64_t`. This is a deliberate,
documented simplification rather than an arbitrary-precision integer
(which real chains like Bitcoin require at their scale and multi-year
timescales) — appropriate for forgechain's educational scope and
realistic testing timeframes.

`uint64_t` overflows at difficulty values far beyond what this project
ever mines in practice (see the accompanying implementation issue for
the numeric bound and the difficulty sanity cap this implies — a single
block's `work(block) = 2^difficulty_` term must itself fit safely below
the `uint64_t` ceiling, independent of how many blocks accumulate on top
of it). This is a known, accepted limitation, not an oversight.

## 5. What this document does not cover

- The mechanics of actually performing a reorg (rolling back and
  reapplying ledger state) — see the reorg implementation issue.
- How deep a fork is allowed to be before it's rejected outright
  (`MAX_REORG_DEPTH`) — see the corresponding issue.
- Wire-protocol changes, if any, needed to exchange competing-chain
  information between peers.
