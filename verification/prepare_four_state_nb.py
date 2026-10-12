from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'verification/tb_four_state.sv').read_text()
replacements={
 'module tb_four_state;':'module tb_nb_four_state;',
 'wire [31:0] rsp_data;':'wire [63:0] rsp_data;',
 'wire [7:0] rsp_id;':'wire [15:0] rsp_id;',
 'wire rsp_error,flush_ready,flush_done,flush_error;':'wire [1:0] rsp_error;\n    wire flush_ready,flush_done,flush_error;',
 'reg bid=0,rid=0,rlast=0;':'reg [1:0] bid=0,rid=0;reg rlast=0;',
 'cache dut (':'cache_nb dut (',
 'dut.state':'dut.phase',
 'rsp_data!==expected||rsp_id!==8\'h37':'rsp_data[31:0]!==expected||rsp_id[7:0]!==8\'h37',
 'FOUR STATE PASS':'NB FOUR STATE PASS',
}
for anchor,value in replacements.items():
    if source.count(anchor)!=1:raise ValueError(f'nonunique anchor {anchor}')
    source=source.replace(anchor,value)
out=ROOT/'verification/tb_nb_four_state.sv'
out.write_text(source,encoding='ascii',newline='\n')
assert all(b in (9,10) or 32<=b<=126 for b in out.read_bytes())
print('NB FOUR STATE TB generated; ASCII PASS')
