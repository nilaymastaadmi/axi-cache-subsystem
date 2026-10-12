module cache_nb #(
    parameter integer SETS=4,
    parameter integer WORDS=4
) (
    input wire clk, input wire rst_n,
    input wire [1:0] req_valid, output wire [1:0] req_ready,
    input wire [63:0] req_addr, input wire [1:0] req_write,
    input wire [63:0] req_data, input wire [7:0] req_mask,
    input wire [15:0] req_id,
    output wire [1:0] rsp_valid, input wire [1:0] rsp_ready,
    output wire [63:0] rsp_data, output wire [15:0] rsp_id,
    output wire [1:0] rsp_error,
    input wire flush_valid, output wire flush_ready,
    output wire flush_done, input wire flush_done_ready,
    output reg flush_error, output reg protocol_error,
    output wire awvalid, input wire awready, output wire [31:0] awaddr,
    output wire [1:0] awid, output wire [7:0] awlen, output wire [2:0] awsize,
    output wire [1:0] awburst, output wire awlock,
    output wire [3:0] awcache, output wire [2:0] awprot, output wire [3:0] awqos,
    output wire wvalid, input wire wready, output wire [31:0] wdata,
    output wire [3:0] wstrb, output wire wlast,
    input wire bvalid, output wire bready, input wire [1:0] bid, input wire [1:0] bresp,
    output wire arvalid, input wire arready, output wire [31:0] araddr,
    output wire [1:0] arid, output wire [7:0] arlen, output wire [2:0] arsize,
    output wire [1:0] arburst, output wire arlock,
    output wire [3:0] arcache, output wire [2:0] arprot, output wire [3:0] arqos,
    input wire rvalid, output wire rready, input wire [31:0] rdata,
    input wire [1:0] rid, input wire [1:0] rresp, input wire rlast,
    output wire [1:0] event_hit, output wire [1:0] event_miss
);
    localparam integer OFFSET=$clog2(WORDS)+2;
    localparam [1:0] NORMAL=0, FLUSHING=1, FLUSH_RESULT=2;
    reg [1:0] phase;
    reg [1:0] active, read_outstanding, flush_started, flush_finished;
    reg [$clog2(WORDS):0] read_count[0:1];
    reg write_busy, write_owner, write_turn;
    reg ar_busy, ar_owner, ar_turn;
    reg rbuf_valid;
    reg [31:0] rbuf_data;
    reg [1:0] rbuf_id, rbuf_resp;
    reg rbuf_last;
    wire [1:0] bank_req_valid[0:1], bank_req_ready[0:1];
    wire [1:0] bank_rsp_valid[0:1];
    wire [31:0] bank_rsp_data[0:1];
    wire [7:0] bank_rsp_id[0:1];
    wire [1:0] bank_rsp_error, bank_flush_ready, bank_flush_done, bank_flush_error;
    wire [1:0] bank_awvalid, bank_wvalid, bank_bready, bank_arvalid, bank_rready;
    wire [31:0] bank_awaddr[0:1], bank_wdata[0:1], bank_araddr[0:1];
    wire [7:0] bank_awlen[0:1],bank_arlen[0:1];
    wire [2:0] bank_awsize[0:1],bank_arsize[0:1],bank_awprot[0:1],bank_arprot[0:1];
    wire [1:0] bank_awburst[0:1],bank_arburst[0:1];
    wire [3:0] bank_awcache[0:1],bank_arcache[0:1],bank_awqos[0:1],bank_arqos[0:1],bank_wstrb[0:1];
    wire [1:0] bank_awlock,bank_arlock,bank_wlast;
    wire [1:0] write_candidates=bank_awvalid|bank_wvalid;
    wire choose_write=write_candidates[write_turn]?write_turn:!write_turn;
    wire choose_read=bank_arvalid[ar_turn]?ar_turn:!ar_turn;
    wire admission=phase==NORMAL&&!flush_valid&&!protocol_error;
    wire [63:0] local_addr={1'b0,req_addr[63:32+OFFSET+1],req_addr[32+OFFSET-1:32],
                           1'b0,req_addr[31:OFFSET+1],req_addr[OFFSET-1:0]};

    initial if (SETS<4 || (SETS&(SETS-1))!=0) $fatal(1,"nonblocking needs power-of-two SETS>=4");
    genvar bank,p;
    generate for(bank=0;bank<2;bank=bank+1)begin:banks
        for(p=0;p<2;p=p+1)begin:ports
            assign bank_req_valid[bank][p]=req_valid[p]&&!active[p]&&admission&&
                req_addr[32*p+OFFSET]==1'(bank);
        end
        cache #(.SETS(SETS/2),.WORDS(WORDS)) core (
            .clk(clk),.rst_n(rst_n),.req_valid(bank_req_valid[bank]),.req_ready(bank_req_ready[bank]),
            .req_addr(local_addr),.req_write(req_write),.req_data(req_data),.req_mask(req_mask),.req_id(req_id),
            .rsp_valid(bank_rsp_valid[bank]),.rsp_ready(rsp_ready),.rsp_data(bank_rsp_data[bank]),
            .rsp_id(bank_rsp_id[bank]),.rsp_error(bank_rsp_error[bank]),
            .flush_valid(phase==FLUSHING&&!flush_started[bank]),.flush_ready(bank_flush_ready[bank]),
            .flush_done(bank_flush_done[bank]),.flush_done_ready(phase==FLUSHING),.flush_error(bank_flush_error[bank]),
            .awvalid(bank_awvalid[bank]),.awready(awready&&write_busy&&write_owner==1'(bank)&&!protocol_error),
            .awaddr(bank_awaddr[bank]),.awid(),.awlen(bank_awlen[bank]),.awsize(bank_awsize[bank]),
            .awburst(bank_awburst[bank]),.awlock(bank_awlock[bank]),.awcache(bank_awcache[bank]),
            .awprot(bank_awprot[bank]),.awqos(bank_awqos[bank]),
            .wvalid(bank_wvalid[bank]),.wready(wready&&write_busy&&write_owner==1'(bank)&&!protocol_error),
            .wdata(bank_wdata[bank]),.wstrb(bank_wstrb[bank]),.wlast(bank_wlast[bank]),
            .bvalid(bvalid&&write_busy&&write_owner==1'(bank)&&!protocol_error),.bready(bank_bready[bank]),
            .bid(bid!={1'b0,write_owner}),.bresp(bresp),
            .arvalid(bank_arvalid[bank]),.arready(arready&&ar_busy&&ar_owner==1'(bank)&&!protocol_error),
            .araddr(bank_araddr[bank]),.arid(),.arlen(bank_arlen[bank]),.arsize(bank_arsize[bank]),
            .arburst(bank_arburst[bank]),.arlock(bank_arlock[bank]),.arcache(bank_arcache[bank]),
            .arprot(bank_arprot[bank]),.arqos(bank_arqos[bank]),
            .rvalid(rbuf_valid&&rbuf_id==2'(bank)&&!protocol_error),.rready(bank_rready[bank]),
            .rdata(rbuf_data),.rid(1'b0),.rresp(rbuf_resp),.rlast(rbuf_last),
            .event_hit(event_hit[bank]),.event_miss(event_miss[bank])
        );
    end endgenerate
    generate for(p=0;p<2;p=p+1)begin:responses
        assign req_ready[p]=!active[p]&&admission&&bank_req_ready[req_addr[32*p+OFFSET]][p];
        assign rsp_valid[p]=bank_rsp_valid[0][p]|bank_rsp_valid[1][p];
        assign rsp_data[32*p+:32]=bank_rsp_valid[1][p]?bank_rsp_data[1]:bank_rsp_data[0];
        assign rsp_id[8*p+:8]=bank_rsp_valid[1][p]?bank_rsp_id[1]:bank_rsp_id[0];
        assign rsp_error[p]=bank_rsp_valid[1][p]?bank_rsp_error[1]:bank_rsp_error[0];
    end endgenerate

    assign awvalid=write_busy&&bank_awvalid[write_owner]&&!protocol_error;
    assign awaddr={bank_awaddr[write_owner][30:OFFSET],write_owner,bank_awaddr[write_owner][OFFSET-1:0]};
    assign awid={1'b0,write_owner};
    assign awlen=bank_awlen[write_owner];assign awsize=bank_awsize[write_owner];
    assign awburst=bank_awburst[write_owner];assign awlock=bank_awlock[write_owner];
    assign awcache=bank_awcache[write_owner];assign awprot=bank_awprot[write_owner];assign awqos=bank_awqos[write_owner];
    assign wvalid=write_busy&&bank_wvalid[write_owner]&&!protocol_error;
    assign wdata=bank_wdata[write_owner];assign wstrb=bank_wstrb[write_owner];assign wlast=bank_wlast[write_owner];
    assign bready=write_busy&&bank_bready[write_owner]&&!protocol_error;
    assign arvalid=ar_busy&&bank_arvalid[ar_owner]&&!protocol_error;
    assign araddr={bank_araddr[ar_owner][30:OFFSET],ar_owner,bank_araddr[ar_owner][OFFSET-1:0]};
    assign arid={1'b0,ar_owner};
    assign arlen=bank_arlen[ar_owner];assign arsize=bank_arsize[ar_owner];
    assign arburst=bank_arburst[ar_owner];assign arlock=bank_arlock[ar_owner];
    assign arcache=bank_arcache[ar_owner];assign arprot=bank_arprot[ar_owner];assign arqos=bank_arqos[ar_owner];
    assign rready=!rbuf_valid&&!protocol_error;
    assign flush_ready=phase==NORMAL&&active==0&&read_outstanding==0&&!write_busy&&!ar_busy&&!rbuf_valid&&!protocol_error;
    assign flush_done=phase==FLUSH_RESULT;

    integer port_index;
    always @(posedge clk)begin
        if(!rst_n)begin
            phase<=NORMAL;active<=0;read_outstanding<=0;
            flush_started<=0;flush_finished<=0;flush_error<=0;protocol_error<=0;
            write_busy<=0;write_owner<=0;write_turn<=0;
            ar_busy<=0;ar_owner<=0;ar_turn<=0;
            read_count[0]<=0;read_count[1]<=0;
            rbuf_valid<=0;rbuf_data<=0;rbuf_id<=0;rbuf_resp<=0;rbuf_last<=0;
        end else begin
            for(port_index=0;port_index<2;port_index=port_index+1)begin
                if(req_valid[port_index]&&req_ready[port_index])active[port_index]<=1;
                if(rsp_valid[port_index]&&rsp_ready[port_index])active[port_index]<=0;
            end
            if(!write_busy&&|write_candidates&&!protocol_error)begin
                write_busy<=1;write_owner<=choose_write;
            end
            if(bvalid&&bready)begin write_busy<=0;write_turn<=!write_owner;end
            if(!ar_busy&&|bank_arvalid&&!protocol_error)begin
                ar_busy<=1;ar_owner<=choose_read;
            end
            if(arvalid&&arready)begin
                ar_busy<=0;ar_turn<=!ar_owner;read_outstanding[ar_owner]<=1;
                read_count[ar_owner]<=0;
            end
            if(rvalid&&rready)begin
                if(rid[1]||!read_outstanding[rid[0]])protocol_error<=1;
                else begin
                    rbuf_valid<=1;rbuf_data<=rdata;rbuf_id<=rid;rbuf_resp<=rresp;rbuf_last<=rlast;
                end
            end
            if(rbuf_valid&&bank_rready[rbuf_id[0]]&&!protocol_error)begin
                rbuf_valid<=0;
                if(read_count[rbuf_id[0]]==($clog2(WORDS)+1)'(WORDS-1))
                    read_outstanding[rbuf_id[0]]<=0;
                else read_count[rbuf_id[0]]<=read_count[rbuf_id[0]]+1'b1;
            end
            case(phase)
                NORMAL: if(flush_valid&&flush_ready)begin
                    phase<=FLUSHING;flush_started<=0;flush_finished<=0;flush_error<=0;
                end
                FLUSHING:begin
                    flush_started<=flush_started|bank_flush_ready;
                    flush_finished<=flush_finished|bank_flush_done;
                    if(|(bank_flush_done&bank_flush_error))flush_error<=1;
                    if(&(flush_finished|bank_flush_done))phase<=FLUSH_RESULT;
                end
                FLUSH_RESULT:if(flush_done_ready)phase<=NORMAL;
                default:phase<=NORMAL;
            endcase
        end
    end
`ifdef CACHE_ASSERT
    always @(posedge clk)if(rst_n&&!protocol_error)begin
        assert (!(bank_rsp_valid[0][0]&&bank_rsp_valid[1][0]));
        assert (!(bank_rsp_valid[0][1]&&bank_rsp_valid[1][1]));
        assert ((rsp_valid&~active)==0);
        if(arvalid&&arready)assert(!read_outstanding[ar_owner]);
        if(rbuf_valid)assert(!rbuf_id[1]&&read_outstanding[rbuf_id[0]]);
    end
`endif
endmodule
