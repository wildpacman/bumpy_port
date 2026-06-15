---
name: source-port-reverse-engineering
description: Use when developing a source port, reimplementation, compatibility layer, or preservation project from legacy binaries and original assets without source code, especially when behavior, formats, rendering, timing, quirks, or platform APIs must be recovered accurately.
---

# Source Port Reverse Engineering

## Core Principle

Build from evidence, not resemblance. Preserve originals, separate confirmed
facts from hypotheses, and make every recovered behavior reproducible before
porting it.

**REQUIRED SUB-SKILLS:** Use `superpowers:systematic-debugging`,
`superpowers:test-driven-development`, and
`superpowers:verification-before-completion` when available.

## Start Every Project

1. Inventory binaries, assets, tools, target platforms, and legal constraints.
2. Hash every supplied original file and make originals read-only by policy.
3. Add `.gitignore` rules before generating artifacts or downloading tools.
4. Establish reproducible build, reference-run, analysis, and verification commands.
5. Create tracked status, evidence catalogs, and an implementation plan.

Keep generated disassembly, captures, unpacked binaries, emulator state, and
downloaded tools outside tracked source unless they are small, intentional,
and required for reproducibility.

## Evidence Ladder

Classify every claim:

| Status | Required basis |
|---|---|
| Structural fact | Deterministic bytes, hashes, dimensions, offsets, or listings |
| Hypothesis | Plausible interpretation awaiting independent evidence |
| Confirmed | Static evidence plus runtime observation or exhaustive validation |
| Implemented | Confirmed behavior transcribed into native code with tests |
| Equivalent | Automated comparison against approved reference checkpoints |

Never promote structural facts directly to semantics. Never infer missing
bytes, algorithms, palettes, hitboxes, or timing from screenshots.

Read [references/evidence-contract.md](references/evidence-contract.md) when
defining reports, catalogs, traces, or confirmation criteria.

## Recovery Workflow

1. Protect and verify originals before analysis.
2. Recover the smallest bounded execution path needed for the milestone.
3. Trace data backward from observable behavior to loaders, globals, and assets.
4. Export deterministic static evidence from the disassembler/decompiler.
5. Gather bounded runtime evidence with explicit start and stop conditions.
6. Write a failing contract test for the report or behavior.
7. Publish machine-readable evidence with addresses, hashes, and coverage.
8. Implement only confirmed behavior behind platform-independent boundaries.
9. Compare native and reference outputs exactly; investigate mismatches.
10. Reverify originals and repository cleanliness.

If automation is unreliable, document the blocker and move to independent
work. Do not publish a false capture or silently approve an approximation.

## Architecture Boundaries

Prefer these layers:

- original asset verification and checked readers;
- format decoders with complete byte consumption;
- pure state machines and renderers;
- thin platform adapters for input, audio, timing, and presentation;
- deterministic dump/replay modes;
- exact reference comparison tools.

Preserve confirmed integer widths, signedness, overflow, timing, limitations,
and bugs unless the project explicitly chooses compatibility-breaking changes.

## Completion Gate

Do not call a format recovered while bytes, commands, fields, termination, or
error behavior remain unexplained. Do not call a milestone equivalent without
automated approved checkpoints.

Before committing or publishing:

- run the relevant full verification command;
- verify original hashes before and after reference execution;
- reject unexpected tracked or generated changes;
- inspect `.gitignore` and tracked binaries;
- record confirmed facts, remaining hypotheses, and the next blocker.

## Common Mistakes

| Mistake | Correction |
|---|---|
| Naming a function from intuition | Keep it a hypothesis until static and runtime evidence agree |
| Treating a screenshot as format evidence | Use screenshots only for observable-output comparison |
| Writing the port before decoding the format | Publish complete format evidence first |
| Silently skipping unknown commands or trailing bytes | Fail explicitly and keep the task open |
| Automating an unstable reference run | Bound it, instrument it, or document the blocker |
| Improving old behavior during recovery | First reproduce; isolate intentional modernization later |
| Committing original assets or generated tools | Hash originals and ignore generated/downloaded artifacts |
