# Executed tool envelope

Linux/WSL, Python 3.12.3, g++, Verilator 5.051 devel
v5.050-99-gf8fb1d664 (mod), Icarus 14 devel
s20260301-322-ga4989d023-dirty, Yosys 0.67+94 git7defa5186 (dirty),
SBY v0.67-4-gfea6e46, Z3 4.15.5. Local smoke logs retain binary SHA256 values.
Alternative solver smoke: Bitwuzla 0.9.1, Boolector 3.2.4, Yices 2.7.0.
Modified/development tools are disclosed, not an immutable official release tag.

Use an installed OSS CAD Suite. Set OSS_CAD_BIN to its bin directory or use
$HOME/tools/oss-cad-suite/bin. Shell wrappers add that path and fail on missing
commands. No tools, licensed libraries, credentials or dependencies are vendored
in the candidate archive. Different tool versions require a new evidence run;
the script output must not be represented as these local results automatically.

No Xcelium, VCS or Questa executable was found on PATH at tool smoke. Executable
UVM verification remains unavailable in this environment. Actual C++/Verilator,
Icarus four-state and formal runs are used instead. Untested UVM skeletons are
not provided as evidence. Generic synthesis is not area/Fmax or physical signoff.

Solver amendment: concurrent Z3 runs exceeded the registered 600-second limit
at both 4x4 and reduced 4x2; Bitwuzla smoke then passed and the unchanged 4x2
properties passed depth24. Pin Bitwuzla for new concurrent and augmented-blocking
checks. Keep Z3 selectable for comparisons and preserve its TIMEOUT outputs.
The independent reviewer also saw two Z3 timeouts on augmented blocking
depth24; one of three Z3 attempts passed, including the author's earlier run.
A solver switch changes execution strategy, not the DUT, property or
assumptions; it does not convert an earlier timeout into a pass.
