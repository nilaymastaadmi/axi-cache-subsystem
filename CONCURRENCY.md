# Concurrent revision contract, frozen before RTL

Baseline accepted: results/blocking-closure-001/closure.json, source snapshot
baseline/manifest.json. The release gate (formal/review/integration) remains open.

Use two interleaved cache banks. Each bank reuses the accepted two-way controller
and owns one miss-status slot (request, reserved victim, refill buffer, progress,
response). This gives two independent outstanding misses, one per bank. Bank bit
is the lowest set-index bit, address bit log2(WORDS)+2. Each bank has SETS/2 sets;
total capacity and associativity equal the blocking cache at the same SETS/WORDS.
Supported SETS >= 4, power of two. Remove bank bit for local lookup; restore it
on AXI addresses. Cross-bank hit-under-miss is allowed. Same-bank operations,
including different sets and a held response, stall. No miss merging. This is
coarse bank reservation, not a claim of arbitrary hit-under-miss or associative
MSHR allocation. Later measurements must expose its limitations.

Two requesters, each with one outstanding operation. Requests in separate banks
may accept together. Each bank arbitrates round robin. Each requester gets its
own held 32-bit response/data/error/8-bit identity. Cross-requester order is not
defined; same-bank visibility follows acceptance order. Different banks hold
disjoint bytes. No coherent/external access to cached memory or MMIO.

AXI profile: blocking profile except 2-bit IDs, ID 0 for bank 0 and 1 for bank 1.
At most two read bursts outstanding with ID-directed beats that may interleave.
AW/W select is locked from first arbitration to B consumption: no W interleave,
one outstanding write-back globally. AW and W remain independent. Read address
selection is held until handshake. A one-beat registered R buffer allows finite
backpressure without an input-to-output combinational path; bubble between beats
is accepted. Internal bank RID/BID are translated to zero only for matching IDs.
Wrong BID fails the active write-back and preserves dirty state. An R beat with
unused or non-outstanding ID is an unrecoverable protocol fault: sticky
protocol_error, stop new admissions/AXI progress, require coordinated reset.
A slave falsely using another live ID cannot be distinguished from legal traffic;
the environment must obey ID association. RRESP errors on a valid ID drain its
full burst and fail that requester, independent of the other bank.
Early/unsolicited B has no diagnostic analogous to protocol_error on R. It is
not accepted before the active write completes AW/W. The slave must associate
B with that write; a permanently stalled malformed response needs coordinated
reset. The non-AXI requester READY depends combinationally on VALID/address;
requester VALID and payload must be driven independently of READY.

Fatal-containment clarification from review: protocol_error suppresses AXI VALID
as well as READY, including a previously offered transaction. Normal stable-VALID
guarantees therefore assume compliant ID association. This is a system-fault
boundary, not legal AXI cancellation/recovery; coordinated reset of both ends is
mandatory and may discard dirty/inflight state. No successful recovery/persistence
claim is made for a malformed slave.

Flush valid prevents admission immediately, waits for all requester responses
and AXI activity to drain, then starts both bank scans. It aggregates both held
done/error responses into one held result. One bank's failure does not roll back
the other's completed flush. Retry handles retained state. Destructive reset
remains coordinated with the AXI slave. No autonomous timeout/cancellation.

Verification must witness two simultaneous outstanding ARs, out-of-order
completion and interleaved beats, cross-bank hit while refill stalled, same-bank
stall, both requester identities, response backpressure, dirty write serialization,
flush drain, valid-ID errors and wrong-BID retention. No count of concurrent
misses without the external AR/last-beat witness. New development seeds 1-8,
1000 operations each; later held-out seeds 201-220, 2000 operations each after
controls and directed gate, distinct from baseline. Compare identical workloads.

Concurrent fault controls registered before execution: corrupted R-buffer data,
flipped R-buffer ID, bank-bit address reconstruction, write-owner unlock before
B, dropped flush error, requester identity corruption. Each must compile and
execute a failing assertion or architectural/protocol witness. Extend directed
checks to all RRESP error positions, early/missing RLAST per bank, invalid-ID
containment and reset recovery, destructive dirty reset and inflight read/write
reset. Account reset cancellations and truncated/orphan burst beats explicitly.
Checker controls: empty/missing terminal, truncated JSON, request/beat count
mismatch, missing coverage, stale source and incorrect maximum concurrency.
