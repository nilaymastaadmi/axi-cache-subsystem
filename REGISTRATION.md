# Execution populations registered before implementation

Local registration, not a committed/public preregistration (commits require
approval). Development seeds 1-8, 1000 random requester operations each. Final
holdout seeds 101-120, 2000 operations each, opened only after directed tests,
development seeds and compiled controls pass. No silent reseed or failure removal.
Cycle timeout 200000 per run. Fault runs use directed traffic with timeout 200000.
Each run gets a new directory and preserves compile/run logs and source hashes.

Traffic: two requesters, randomized independent response readiness and channel
delays; mixed reads/writes, all byte masks, hot/streaming/conflict addresses.
Architectural reference uses byte map, with acceptance-driven updates and
failed-operation rollback. AXI slave has separate backing bytes. Every dirty
write beat and final successful flush compare against architecture.

Compiled faults: dirty-bit omission, hit tag corruption, byte mask inversion,
premature refill before B, unstable W payload, response identity corruption.
Require baseline green first, compile each variant, name first failing witness.
Fault that merely fails to compile is not detected. Divergence followed by
timeout remains a design failure, never a timeout-only void.

Evidence checker controls: empty log, missing terminal summary, truncated JSON,
stale source hashes, count mismatch. Require explicit rejection and intact
baseline acceptance. Formal control must remove/check its relevant assumption.

Performance populations (not opened yet): identical fixed locality, streaming
and conflict word requests, memory latency 2/10/40 cycles, with dirty flush
included. Report cycle completion, requests/cycle, hit/miss, transferred beats
and bytes, including cache losses. Simulated timing is not silicon timing.

Follow-up amendment, before the G1 runs: failed BRESP/wrong BID now leaves
backing bytes uncommitted while every W byte is still checked. Successful B
keeps normal write effects. Require stale backing after failed requester eviction
and seven compiled faults in both suites, including clearing dirty in the
request-path B-error branch. This mutant survived the earlier slave models;
retain that negative control result as historical evidence. Banked wrong-BID
tests include both unused ID2 and the other bank's live ID. Required four-state
ID tests include an excess ID0 beat after a counted burst has completed.
Current banked formal default is depth32; dirty-clear cover requires request
path, separately from flush invalidation. Populations already opened remain
confirmation runs. Registration is local/self-attested, not externally timestamped.
