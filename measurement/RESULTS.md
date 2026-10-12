# Simulated comparison report

Status: verified locally by scripts/compare.sh --out results/review-comparison-001.
Fixed protocol and pre-execution amendment: measurement/PROTOCOL.md. Each row
uses 4096 requests, cold start, two requesters and final dirty flush. All 45 rows
passed; final memory images equal across three variants for all five workloads.
Raw rows include mean/max accepted-to-response latency, hit/miss, beats/bytes
and image hashes. review-comparison-controls-001 detects one compiled byte fault
and rejects five corrupt-evidence controls. Old comparison-003 remains retained.

| Workload | Memory latency | Blocking cycles | Banked cycles | Bypass cycles | Cache bytes | Bypass bytes |
|---|---:|---:|---:|---:|---:|---:|
| locality | 2 | 13694 | 6872 | 22187 | 64 | 16384 |
| streaming | 2 | 30704 | 17973 | 22187 | 32768 | 16384 |
| conflict | 2 | 56330 | 38420 | 22187 | 81920 | 16384 |
| same_bank | 2 | 13695 | 13704 | 22187 | 64 | 16384 |
| same_bank_streaming | 2 | 30716 | 35843 | 22187 | 32768 | 16384 |
| locality | 10 | 13727 | 6896 | 56321 | 64 | 16384 |
| streaming | 10 | 46088 | 25697 | 56321 | 32768 | 16384 |
| conflict | 10 | 97287 | 58905 | 56321 | 81920 | 16384 |
| same_bank | 10 | 13727 | 13736 | 56321 | 64 | 16384 |
| same_bank_streaming | 10 | 46088 | 51210 | 56321 | 32768 | 16384 |
| locality | 40 | 13847 | 6986 | 179195 | 64 | 16384 |
| streaming | 40 | 107528 | 56567 | 179195 | 32768 | 16384 |
| conflict | 40 | 250887 | 135742 | 179195 | 81920 | 16384 |
| same_bank | 40 | 13846 | 13856 | 179195 | 64 | 16384 |
| same_bank_streaming | 40 | 107528 | 112651 | 179195 | 32768 | 16384 |

Locality has 4094 hits of 4096: throughput improves by serving two independent
banks' hits in parallel. At latency10 mean accepted-response latency remains
about 3.34 cycles blocking and 3.35 banked. This is not a miss-overlap speedup.
same_bank_streaming forces both requester streams to miss in bank0. At latency2
banked takes 35843 cycles versus 30716 blocking and 22187 bypass; coarse bank
reservation removes parallelism and the registered R bubble adds cost. At
latency40 banked 112651 still exceeds blocking 107528; bypass 179195 is slower.
The real serial RV32 program also loses with both caches: blocking 2924,
banked 3246, bypass 2232 cycles (integration-002/results.json).

Traffic amplification is retained: locality 64 external bytes, streaming 32768,
conflict 81920 versus bypass 16384. Full-line fills/write-back account for this,
not a measured physical bandwidth claim. Bypass has one outstanding slot;
banked has two. The comparison does not isolate policy against an equally
concurrent uncached bridge. Blocking and banked preserve cache geometry.

Schedules use identical deterministic cycle rules but react to each design's
completion timing. Accepted-response latency excludes pre-admission queueing;
total cycles include it, transfers and flush. These are synthetic RTL cycles,
not Fmax, CPU IPC, real DRAM timings or commercial-workload results.
