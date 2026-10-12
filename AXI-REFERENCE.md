# AXI requirement source

Pinned source: Arm IHI 0022H, ID040120, issue H dated 31 March 2020.
Read from the official PDF on 12 October 2026. Not a newest-issue claim.
https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/IHI0022H_amba_axi_protocol_spec.pdf

| Clause / printed page | Project mapping |
|---|---|
| A3.2.1 / A3-41 | handshake and stable source; registered outputs |
| A3.2.2 / A3-42-43 | five independently checked channels |
| A3.3.1 / A3-44-46 | AW/W independence and B completion dependency |
| A3.4.1 / A3-48-49 | full line INCR address, length, size, 4 KiB boundary |
| A3.4.4 / A3-54 | byte enables |
| A3.4.5 / A3-59-60 | response errors; drain all read beats |

REQUIREMENTS R4-R5 freeze the project's restricted profile. This source does
not certify our implementation. SPEC source hash will be recorded if downloaded.
