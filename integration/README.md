# RV32 data-cache integration example

The copied MIT-licensed RV32I core is AI-assisted. Exact source/license hashes
and original revision are in PROVENANCE.json. The original checkout is unchanged.
This example is new evidence, unrelated to the older upstream CPU-test result.

rv32_cache_top.sv instantiates the actual core and selectable cache/bypass RTL.
The C++ harness supplies an uncached instruction ROM and pauses the core clock
while a data operation completes through valid/ready requests and actual cache
response data. It then clocks exactly one instruction. This is a simulation
adapter demonstrating functional integration, not a synthesized stallable CPU
pipeline or a glitch-free hardware clock-gating implementation. Cache/AXI clocks
continue while the core is paused. MMIO and self-modifying code are excluded.

The fixed program executes aligned words, byte and halfword stores, byte loads,
64 iterations of dirty stores/loads across both banks and set conflicts. Literal
checks verify selected retire values and all final stored words. Full retire
traces and backing byte images must match blocking, banked and bypass variants.
The shared AXI/byte checker also validates every accepted data operation and W
byte, and final successful flush. The CPU receives captured RTL response bytes,
never the reference-map expected value. No cache clock edge retires the core.

Run `bash scripts/integration.sh --out results/NEW-INTEGRATION` from project root.
Scoped core_waivers.vlt waives only inherited unused funct7 and OP_FENCE
declarations; all other integration lint warnings remain fatal. This program is
a focused integration example, not independent complete RV32I ISA validation.
