# Next engineering gates

The initial source snapshot is commit b514520. The accepted replay and bounded
formal evidence are documented in README.md, TOOLS.md and evidence/README.md.
AI assistance and negative measurements remain disclosed.

1. Add a two-live-read uncached comparison endpoint, retain the original
   one-outstanding baseline, verify aliases/flush and falsifying controls,
   and report all results. Equal maximum concurrency does not imply equal
   arbitration, slot ownership or traffic.
2. Add a common-clock architectural enable and RTL memory-request bridge to
   a separate derivative of the MIT RV32 core. Preserve the vendor source.
   Require retired trace/image equality and request-disconnect controls.
3. Decompose the open byte-data formal proof, with registered assumptions,
   nonvacuous covers and actual mutation counterexamples. Preserve timeouts.
4. Review substantive RTL changes independently before accepting a new source
   snapshot. Keep reproducible evidence tied to source identity.

These are ongoing gates, not completed features. Coherence, MMIO, ECC, SRAM
macros, physical signoff and licensed UVM execution are outside this snapshot.
