# Cache IP contract, revision 1

Status: changed locally. AI-assisted engineering. Local isolated copy, no Git
repository or publication. This contract precedes RTL implementation.

## Requesters and storage

R1: Two independent valid/ready requesters. Each request carries 32-bit address,
write flag, 32-bit data, four byte enables and 8-bit opaque identity. An accepted
request is irrevocable until response or destructive reset. Requester holds
payload stable until ready. Responses hold data/error/identity stable until ready.
Each requester may have at most one outstanding request. Round-robin admission
breaks simultaneous requests; turns change on acceptance. Blocking revision has
one total outstanding operation, including a blocked response. Acceptance order
is visibility order. Successful store response means bytes are retained in cache,
not persistent in backing memory. Loads return a complete aligned word.
Requester VALID must not depend combinationally on READY. READY depends on
VALID arbitration, and the banked READY also depends on the request address.
An integrator must hold those request signals independently to avoid a loop.

R2: Little-endian, aligned 32-bit accesses only. Unaligned requests complete with
error and no memory activity. Zero mask is a successful no-op store. Address map
is ordinary cacheable memory only; MMIO and external writers are excluded. Users
must bypass MMIO outside this module. Reset is destructive and may lose dirty
data. Reset must be coordinated with AXI slave: old responses must be discarded
by both ends. Independent mid-transaction reset is unsupported, not cancellation.

R3: Two ways; power-of-two SETS >= 2 and WORDS >= 2; WORDS <= 256. Line size
4*WORDS <= 1024 bytes, aligned to that size, so bursts cannot cross 4 KiB.
Default SETS=4, WORDS=4. Geometry must leave at least one address tag bit. Prefer
invalid way, otherwise evict least recently used way (one MRU bit per set).
Byte-write allocate, write-back; hits update replacement and successful allocated
requests update replacement. Invalid data and tags are never observed.
Arrays are register arrays with combinational word read and synchronous writes;
no SRAM macro, capacity/area/Fmax claim. No coherence or ECC.

## AXI profile and failures

R4: AXI4 master, 32-bit address/data, one-bit ID with constant ID=0 for blocking
revision, one burst outstanding. Full-width INCR, AxSIZE=2, AxLEN=WORDS-1,
aligned line address. AxLOCK=0, AxCACHE=0, AxPROT=0, AxQOS=0. No USER/REGION
signals, exclusives, narrow/unaligned transfers or WRAP/FIXED bursts. AW, W, B,
AR, R use independent handshakes. W may finish before AW; BREADY is asserted only
after both AW and all W handshakes. All bytes enabled in eviction. Each VALID
and its payload stays stable until handshake. No combinational READY-to-VALID
path on AXI. Slave may stall any channel indefinitely; liveness requires progress.

R5: Dirty victim is unchanged until write response with BID=0 and BRESP=OKAY.
Non-OKAY or wrong BID completes requesting operation/flush with error; retain
valid dirty victim for retry and issue no refill. Slave may have partially written
back memory on error; cache remains authoritative. No rollback guarantee for
backing memory. A successful eviction may make victim clean before later refill
failure; old line remains valid. Refill uses a separate line buffer, committing
only after all beats arrive with matching RID, OKAY and exact final RLAST.
On error, drain the specified number of beats, report error and leave victim
data/tag unchanged. Wrong RLAST is detected as error, but a malformed slave that
stops beats cannot be recovered without coordinated reset. No timeout that
illegally abandons an AXI transaction.
Failed requester stores leave their cache bytes unchanged. Unsolicited or early
B is unsupported slave behavior: BREADY waits for the completed AW/W burst;
no sticky B-fault diagnostic or autonomous recovery is provided. A stalled B
that never becomes associated with a valid write requires coordinated reset.

R6: Maintenance valid/ready accepts flush only when idle, with priority over new
requests. Caller holds valid until acceptance. Drain dirty lines by write-back,
invalidate each successfully handled line, return a held done/error response.
On first failure stop: failed and unvisited lines retain state; previously flushed
lines remain invalid. Retrying restarts the scan. Successful flush means all
cache state invalid and all successful stores reflected in backing memory.

## Stages and acceptance

R7: Blocking acceptance gate: independent byte memory scoreboard driven at
request acceptance, protocol monitors, exercised directed bins, preregistered
random seeds, reset/error/flush tests, negative controls, lint/synthesis and legal
geometry sweeps. No claim of exhaustive correctness. Preserve raw failing runs.

R8: Concurrent revision must maintain >=2 MSHRs and permit safe hit-under-miss,
with one outstanding request per requester. Response ordering is per requester;
different requesters may finish out of order. Same-line and same-set reserved
victims stall until miss completion. MSHR ID routes refill; finite buffers provide
backpressure. Flush blocks new admission, drains all requests/responses and then
scans. Freeze additional concurrency semantics before extension. Keep blocking
baseline as a runnable source snapshot after its gate passes.

R9: Independent review, bounded formal safety with reachability covers and
falsifying controls, clean export replay, realistic workload/bypass comparison,
integration and evidence audit are release gates. They are not established by
the first simulation pass. Licensed UVM execution is a tool availability gate.
