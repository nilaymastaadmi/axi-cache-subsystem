# Blocking architecture

Single accepted request register and single held response. LOOKUP selects hit
or victim. On miss, EVICT runs independently tracked AW and W transfers; BRESP
decides whether refill is permitted. AR issues a line read; REFILL accumulates
data/error in a temporary buffer. INSTALL commits a complete clean line then
retries LOOKUP, so partial stores use the same byte update path as hits. An
errored refill cannot partially overwrite a resident line. One counter tracks
beat handshakes, never elapsed cycles.

Flush scans flattened set/way index and reuses EVICT. Successful write-back
clears dirty; maintenance invalidates then advances. Failure returns held flush
error while retaining dirty data. Invalid/dirty/replacement state resets; data
storage is not reset. All master VALID outputs derive from registered control.

Verification deliberately differs: C++ byte-addressed architectural memory is
updated on request admission; an independent byte-addressed AXI slave checks
evictions against that memory at each W transfer and compares after flush.
Protocol checks operate only on visible ports, not internal FSM state.

The concurrent milestone will replace the single request slot with two slots,
reserve victim sets and give each refill an AXI ID. It requires its own frozen
ordering and failure contract before changing this architecture.
