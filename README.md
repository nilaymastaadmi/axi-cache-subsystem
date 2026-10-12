# Two-requester write-back cache subsystem

AI-assisted SystemVerilog cache IP and executable verification. Two ways,
aligned 32-bit little-endian requests with byte masks, WB/WA, round-robin
admission, opaque identities, held responses and restricted AXI4 bursts.

rtl/cache.sv is the blocking baseline. rtl/cache_nb.sv composes two interleaved
banks, each with one retained request/victim/refill miss slot. Two read bursts
may be outstanding; IDs route interleaved beats. A hit in the other bank may
complete under a miss. Operations in the same bank stall, including different
sets and held responses. No merging or coherence. Global write ownership locks
AW/W through B. A one-beat R buffer introduces a bubble between accepted beats.
Failed dirty eviction retains authoritative cache data; failed refill does not
install partial data. Flush drains accepted operations before scanning banks.

Read REQUIREMENTS.md, ARCHITECTURE.md and CONCURRENCY.md before integration.
Reset is destructive and must be coordinated with the AXI slave. Ordinary
cacheable RAM only, no MMIO/external writers. Successful stores are retained in
cache, not persistent. Arbitrary stalls have no autonomous timeout; liveness
requires environment progress. Wrong/non-live RID enters sticky protocol fault
and requires coordinated reset. During that fatal state the interface suppresses
AXI activity, including previously offered VALID; normal AXI stability guarantees
apply before fatal containment. A compliant slave must use the issued IDs.

Independent byte references operate on accepted requests, with separate backing
memory and write-effect checks. Directed/random regressions, compiled faults,
fail-closed evidence controls, four-state/X controls, bounded formal checks and
generic synthesis are executable. They do not prove exhaustive correctness,
unbounded liveness, silicon validation, SRAM characteristics or commercial use.

Typical proof commands, from root with installed tools (TOOLS.md):

```
bash scripts/run.sh --out results/NEW-BLOCKING --operations 1000
bash scripts/suite_nb.sh --out results/NEW-CONCURRENT-CLOSURE
bash scripts/four_state_nb.sh results/NEW-FOURSTATE
bash scripts/synth_nb.sh results/NEW-SYNTHESIS
bash scripts/compare.sh --out results/NEW-MEASUREMENT
bash scripts/integration.sh --out results/NEW-INTEGRATION
```

Each run uses a fresh directory and retains failures, actual terminal output,
source hashes and JSON. Licensed UVM is an explicit unavailable-tool gap.
measurement/PROTOCOL.md fixes comparative workloads before measurement.
integration/README.md documents the actual core and simulation clock adapter.
The candidate package is prepared locally. Independent review found no RTL
defect and made release conditional on a failed-write-back observability repair.
Both repaired slave models now leave backing bytes uncommitted on planned B
error/wrong BID, check every W byte, and kill the dirty-loss mutant with a byte
witness. Seven compiled faults are required per simulation suite. The changed
harness has not been independently rereviewed. Formal safety/cover status is a
separate bounded gate; the clean replay terminal explicitly excludes it.
No formal byte-memory invariant, unbounded or liveness proof is claimed.
Publication still requires authorization. AI assistance and negative performance
outcomes remain disclosed in measurement/RESULTS.md and integration/README.md.
