# Specs

A spec is a short design document for a change that needs a decision before the code is written. One file for each, numbered in order: `S0001.md`, `S0002.md`, and so on. A number is never reused.

Each spec starts with its **Status**: `draft` (a proposal, nothing decided), `accepted` (the owner decided; the decisions are in the text), `implemented` (done; the text says where), or `rejected` / `superseded` (and by what). The facts in a spec are marked as in the other documents of `docs/`: **[read]** read in the code or the source, **[run]** run, **[not checked]** said from memory or not checked.

| Spec | Title | Status |
| --- | --- | --- |
| [S0001](S0001.md) | A TLS hook for the transport of `wasi:http`, in C and in Python | accepted |
| [S0002](S0002.md) | A ready TLS backend for the C transport of `wasi:http`, over Mbed TLS | draft, deferred |
