# Fixed comparison protocol, frozen before measurement

AI-assisted synthetic RTL measurements, not silicon timing. Compare the actual
blocking and two-bank cache (4 sets, 2 ways, 4 words/line, 128 data bytes) against
a single-outstanding, uncached AXI word bridge. All three use one identical C++
AXI backing model, byte reference and requester issue/response scheduler.

Four populations: locality (two resident lines), streaming (sequential words in
disjoint bank-interleaved streams), conflict (three tags per requester in one
2-way set), same_bank (two resident lines in bank 0, exposing bank reservation).
4096 requests, 2048 per requester. Each requester has one outstanding operation
and an independent deterministic stream with disjoint bytes from the other.
One in four operations is a masked store, masks cycle 1..15. Requests are aligned.
No reset/error injection in timed runs; separate correctness regressions own it.

Latencies 2, 10, 40 cycles from AR handshake to first offered R, and from completed
AW/W to offered B. One shared R beat/cycle before master backpressure, two read
slots, alternating available IDs. AW/W/AR readiness is deterministic cycle-based
backpressure (ready on 3 of 4 cycles), identical rule across variants. Requester
response ready on 4 of 5 cycles. Cold start, all traffic and final dirty flush
included. No periodic flush, warm-up exclusion, seed search or best-case removal.
Cycle cap 1000000 per population. Keep all 36 rows, including cache losses.

Independent-review amendment, 12 October 2026 before execution: add a fifth
population, same_bank_streaming. Requester p at operation i uses address
(2*floor(i/4)+p)*32+(i%4)*4. Both streams use bank0, disjoint consecutive lines
within that bank, with three subsequent words per allocated line. Keep the same
4096 requests, masked stores, latencies, readiness and final flush rules. Require
nonzero misses in both cache variants. Retain all 45 rows, never select a subset.
The old four-population results remain historical, not overwritten.

Locality mostly hits: its banked throughput improvement reflects parallel hits
in different banks, not evidence of miss-overlap benefit. same_bank_streaming
measures the cost of the coarse reservation when both streams miss in one bank.

Record total cycles, summed/max accepted-to-response latency, throughput,
hit/miss pulses, external read/write bursts and beats/bytes, final flush cycles,
and final architectural/backing equality. Two independent accepted byte maps
must agree, each response checked at its actual accepted identity. Byte effects
checked on every external W and final flush. No statement of measured benefit
until output passes terminal, source hash and exact-count checks.
