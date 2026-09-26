# Implemented compatibility contract

The authoritative surface is now [full-v1](../protocol/README.md), with
[machine-readable requirements](../protocol/full-v1.json) and the
[wire reference](../protocol/wire-v1.md). The earlier proposed API was not the
running implementation and is superseded by this contract.

Supported binaries implement the full surface over standard input/output; the
frontend does not link engine code, inspect process memory or inject DLLs.
