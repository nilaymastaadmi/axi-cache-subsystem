from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'rtl/cache_nb.sv').read_text()
ports=source[source.index(') (')+2:source.index(');')+2]
text='module cache_compare #(parameter integer MODE=0) '+ports+'''
    generate if(MODE==1)begin:nb
        cache_nb core(.*);
    end else if(MODE==0)begin:blocking
        wire [31:0] scalar_data;wire [7:0] scalar_id;wire scalar_error,hit,miss;
        cache core(.rsp_data(scalar_data),.rsp_id(scalar_id),.rsp_error(scalar_error),
            .event_hit(hit),.event_miss(miss),.awid(),.arid(),.bid(|bid),.rid(|rid),.*);
        assign rsp_data={scalar_data,scalar_data};assign rsp_id={scalar_id,scalar_id};
        assign rsp_error={2{scalar_error}}&rsp_valid;
        assign event_hit={1'b0,hit};assign event_miss={1'b0,miss};
        assign awid=0;assign arid=0;assign protocol_error=0;
    end else begin:bypass
        localparam [2:0] IDLE=0,READ=1,RWAIT=2,WRITE=3,BWAIT=4,RESP=5,FLUSH=6;
        reg [2:0] state;reg owner,turn,aw_sent,w_sent,error;
        reg [31:0] address,payload,value;reg [3:0] mask;reg [7:0] identity;
        wire selected=req_valid[turn]?turn:!turn;
        assign req_ready=state==IDLE&&!flush_valid&&|req_valid?(selected?2'b10:2'b01):0;
        assign rsp_valid=state==RESP?(owner?2'b10:2'b01):0;
        assign rsp_data={value,value};assign rsp_id={identity,identity};
        assign rsp_error={2{error}}&rsp_valid;
        assign awvalid=state==WRITE&&!aw_sent;assign wvalid=state==WRITE&&!w_sent;
        assign awaddr=address;assign wdata=payload;assign wstrb=mask;assign wlast=1;
        assign bready=state==BWAIT;assign arvalid=state==READ;assign araddr=address;
        assign rready=state==RWAIT;
        assign awid=0;assign arid=0;assign awlen=0;assign arlen=0;
        assign awsize=2;assign arsize=2;assign awburst=1;assign arburst=1;
        assign awlock=0;assign arlock=0;assign awcache=0;assign arcache=0;
        assign awprot=0;assign arprot=0;assign awqos=0;assign arqos=0;
        assign flush_ready=state==IDLE;assign flush_done=state==FLUSH;
        assign flush_error=0;assign protocol_error=0;assign event_hit=0;assign event_miss=0;
        always @(posedge clk)begin
            if(!rst_n)begin state<=IDLE;owner<=0;turn<=0;aw_sent<=0;w_sent<=0;
                address<=0;payload<=0;mask<=0;identity<=0;value<=0;error<=0;end
            else case(state)
                IDLE: if(flush_valid)state<=FLUSH;
                    else if(|(req_valid&req_ready))begin
                        owner<=selected;turn<=!selected;address<=req_addr[32*selected+:32];
                        payload<=req_data[32*selected+:32];mask<=req_mask[4*selected+:4];
                        identity<=req_id[8*selected+:8];value<=0;error<=0;aw_sent<=0;w_sent<=0;
                        if(req_addr[32*selected+:2]!=0)begin state<=RESP;error<=1;end
                        else if(req_write[selected]&&req_mask[4*selected+:4]==0)state<=RESP;
                        else state<=req_write[selected]?WRITE:READ;
                    end
                READ:if(arready)state<=RWAIT;
                RWAIT:if(rvalid)begin value<=rdata;error<=rresp!=0||rid!=0||!rlast;state<=RESP;end
                WRITE:begin
                    if(awready)aw_sent<=1;if(wready)w_sent<=1;
                    if((aw_sent||awready)&&(w_sent||wready))state<=BWAIT;
                end
                BWAIT:if(bvalid)begin error<=bresp!=0||bid!=0;state<=RESP;end
                RESP:if(rsp_ready[owner])state<=IDLE;
                FLUSH:if(flush_done_ready)state<=IDLE;
                default:state<=IDLE;
            endcase
        end
    end endgenerate
endmodule
'''
out=ROOT/'measurement/cache_compare.sv'
out.write_text(text,encoding='ascii',newline='\n')
assert all(b in (9,10) or 32<=b<=126 for b in out.read_bytes())
print('COMPARISON TOP generated, ASCII PASS')
