module cache #(
    parameter integer SETS = 4,
    parameter integer WORDS = 4
) (
    input wire clk, input wire rst_n,
    input wire [1:0] req_valid, output reg [1:0] req_ready,
    input wire [63:0] req_addr, input wire [1:0] req_write,
    input wire [63:0] req_data, input wire [7:0] req_mask,
    input wire [15:0] req_id,
    output wire [1:0] rsp_valid, input wire [1:0] rsp_ready,
    output reg [31:0] rsp_data, output reg [7:0] rsp_id,
    output reg rsp_error,
    input wire flush_valid, output wire flush_ready,
    output wire flush_done, input wire flush_done_ready,
    output reg flush_error,
    output wire awvalid, input wire awready, output wire [31:0] awaddr,
    output wire awid, output wire [7:0] awlen, output wire [2:0] awsize,
    output wire [1:0] awburst, output wire awlock,
    output wire [3:0] awcache, output wire [2:0] awprot,
    output wire [3:0] awqos,
    output wire wvalid, input wire wready, output wire [31:0] wdata,
    output wire [3:0] wstrb, output wire wlast,
    input wire bvalid, output wire bready, input wire bid,
    input wire [1:0] bresp,
    output wire arvalid, input wire arready, output wire [31:0] araddr,
    output wire arid, output wire [7:0] arlen, output wire [2:0] arsize,
    output wire [1:0] arburst, output wire arlock,
    output wire [3:0] arcache, output wire [2:0] arprot,
    output wire [3:0] arqos,
    input wire rvalid, output wire rready, input wire [31:0] rdata,
    input wire rid, input wire [1:0] rresp, input wire rlast,
    output reg event_hit, output reg event_miss
);
    localparam integer SW = $clog2(SETS);
    localparam integer WW = $clog2(WORDS);
    localparam integer OFFSET = WW + 2;
    localparam integer TAGW = 32 - SW - OFFSET;
    localparam [3:0] IDLE=0, LOOKUP=1, EVICT=2, WB_RESP=3,
        READ_ADDR=4, REFILL=5, INSTALL=6, RESPONSE=7,
        FLUSH_SCAN=8, FLUSH_RESP=9;
    reg [3:0] state;
    reg valid [0:SETS-1][0:1];
    reg dirty [0:SETS-1][0:1];
    reg [TAGW-1:0] tags [0:SETS-1][0:1];
    reg [31:0] data [0:SETS-1][0:1][0:WORDS-1];
    reg mru [0:SETS-1];
    reg [31:0] refill [0:WORDS-1];
    reg [31:0] address, store_data, victim_addr;
    reg [3:0] mask;
    reg write_req, owner, turn, victim_way, maintenance;
    reg [SW-1:0] victim_set;
    reg [WW:0] beat;
    reg [SW:0] scan;
    reg aw_sent, w_sent, read_error, missed;
    localparam [WW:0] LAST_BEAT = (WW+1)'(WORDS-1);
    localparam [SW:0] LAST_SCAN = (SW+1)'(2*SETS-1);
    wire [SW-1:0] set_index = address[OFFSET +: SW];
    wire [WW-1:0] word_index = address[2 +: WW];
    wire [TAGW-1:0] tag_value = address[OFFSET+SW +: TAGW];
    wire hit0 = valid[set_index][0] && tags[set_index][0] == tag_value;
    wire hit1 = valid[set_index][1] && tags[set_index][1] == tag_value;
    wire hit_way = hit1;
    wire choose_way = !valid[set_index][0] ? 1'b0 :
                      !valid[set_index][1] ? 1'b1 : !mru[set_index];
    wire incoming_error = rresp != 0 || rid != 0 || rlast != (beat == LAST_BEAT);
    integer s, w, k, lane;

    initial begin
        if (SETS < 2 || (SETS & (SETS-1)) != 0 || WORDS < 2 ||
            WORDS > 256 || (WORDS & (WORDS-1)) != 0 || TAGW < 1)
            $fatal(1, "unsupported cache geometry");
    end

    always @* begin
        req_ready = 0;
        if (state == IDLE && !flush_valid) begin
            if (req_valid[turn]) req_ready[turn] = 1;
            else req_ready[!turn] = 1;
        end
    end
    assign rsp_valid = state == RESPONSE ? (owner ? 2'b10 : 2'b01) : 2'b00;
    assign flush_ready = state == IDLE;
    assign flush_done = state == FLUSH_RESP;
    assign awvalid = state == EVICT && !aw_sent;
    assign awaddr = victim_addr;
    assign awid = 0;
    assign awlen = 8'(WORDS-1);
    assign awsize = 2;
    assign awburst = 1;
    assign awlock = 0;
    assign awcache = 0;
    assign awprot = 0;
    assign awqos = 0;
    assign wvalid = state == EVICT && !w_sent;
    assign wdata = data[victim_set][victim_way][beat[WW-1:0]];
    assign wstrb = 15;
    assign wlast = beat == LAST_BEAT;
    assign bready = state == WB_RESP;
    assign arvalid = state == READ_ADDR;
    assign araddr = {address[31:OFFSET], {OFFSET{1'b0}}};
    assign arid = 0;
    assign arlen = 8'(WORDS-1);
    assign arsize = 2;
    assign arburst = 1;
    assign arlock = 0;
    assign arcache = 0;
    assign arprot = 0;
    assign arqos = 0;
    assign rready = state == REFILL;

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            turn <= 0;
            owner <= 0;
            address <= 0;
            store_data <= 0;
            mask <= 0;
            write_req <= 0;
            rsp_id <= 0;
            rsp_data <= 0;
            rsp_error <= 0;
            flush_error <= 0;
            victim_set <= 0;
            victim_way <= 0;
            victim_addr <= 0;
            maintenance <= 0;
            scan <= 0;
            beat <= 0;
            aw_sent <= 0;
            w_sent <= 0;
            read_error <= 0;
            missed <= 0;
            event_hit <= 0;
            event_miss <= 0;
            for (s=0; s<SETS; s=s+1) begin
                mru[s] <= 0;
                for (w=0; w<2; w=w+1) begin
                    valid[s][w] <= 0;
                    dirty[s][w] <= 0;
                end
            end
        end else begin
            event_hit <= 0;
            event_miss <= 0;
            case (state)
                IDLE: begin
                    if (flush_valid) begin
                        maintenance <= 1;
                        scan <= 0;
                        flush_error <= 0;
                        state <= FLUSH_SCAN;
                    end else if (|(req_valid & req_ready)) begin
                        owner <= req_ready[1];
                        turn <= !req_ready[1];
                        address <= req_addr[req_ready[1]*32 +: 32];
                        store_data <= req_data[req_ready[1]*32 +: 32];
                        write_req <= req_write[req_ready[1]];
                        mask <= req_mask[req_ready[1]*4 +: 4];
                        rsp_id <= req_id[req_ready[1]*8 +: 8];
                        rsp_error <= 0;
                        rsp_data <= 0;
                        maintenance <= 0;
                        missed <= 0;
                        state <= LOOKUP;
                    end
                end
                LOOKUP: begin
                    if (address[1:0] != 0) begin
                        rsp_error <= 1;
                        state <= RESPONSE;
                    end else if (write_req && mask == 0) begin
                        state <= RESPONSE;
                    end else if (hit0 || hit1) begin
                        mru[set_index] <= hit_way;
                        rsp_data <= write_req ? 32'b0 : data[set_index][hit_way][word_index];
                        if (write_req) begin
                            for (lane=0; lane<4; lane=lane+1)
                                if (mask[lane])
                                    data[set_index][hit_way][word_index][lane*8 +: 8]
                                        <= store_data[lane*8 +: 8];
                            dirty[set_index][hit_way] <= 1;
                        end
                        event_hit <= !missed;
                        state <= RESPONSE;
                    end else begin
                        event_miss <= 1;
                        missed <= 1;
                        victim_way <= choose_way;
                        victim_set <= set_index;
                        victim_addr <= {tags[set_index][choose_way], set_index, {OFFSET{1'b0}}};
                        beat <= 0;
                        aw_sent <= 0;
                        w_sent <= 0;
                        read_error <= 0;
                        if (valid[set_index][choose_way] && dirty[set_index][choose_way])
                            state <= EVICT;
                        else state <= READ_ADDR;
                    end
                end
                EVICT: begin
                    if (awvalid && awready) aw_sent <= 1;
                    if (wvalid && wready) begin
                        if (wlast) w_sent <= 1;
                        else beat <= beat + 1'b1;
                    end
                    if ((aw_sent || awready) && (w_sent || (wready && wlast)))
                        state <= WB_RESP;
                end
                WB_RESP: if (bvalid) begin
                    if (bresp != 0 || bid != 0) begin
                        if (maintenance) begin
                            flush_error <= 1;
                            state <= FLUSH_RESP;
                        end else begin
                            rsp_error <= 1;
                            state <= RESPONSE;
                        end
                    end else begin
                        dirty[victim_set][victim_way] <= 0;
                        if (maintenance) begin
                            valid[victim_set][victim_way] <= 0;
                            if (scan == LAST_SCAN) state <= FLUSH_RESP;
                            else begin scan <= scan + 1'b1; state <= FLUSH_SCAN; end
                        end else begin
                            beat <= 0;
                            state <= READ_ADDR;
                        end
                    end
                end
                READ_ADDR: if (arready) begin
                    beat <= 0;
                    read_error <= 0;
                    state <= REFILL;
                end
                REFILL: if (rvalid) begin
                    refill[beat[WW-1:0]] <= rdata;
                    read_error <= read_error || incoming_error;
                    if (beat == LAST_BEAT) begin
                        if (read_error || incoming_error) begin
                            rsp_error <= 1;
                            state <= RESPONSE;
                        end else state <= INSTALL;
                    end else beat <= beat + 1'b1;
                end
                INSTALL: begin
                    for (k=0; k<WORDS; k=k+1) data[victim_set][victim_way][k] <= refill[k];
                    tags[victim_set][victim_way] <= tag_value;
                    valid[victim_set][victim_way] <= 1;
                    dirty[victim_set][victim_way] <= 0;
                    state <= LOOKUP;
                end
                RESPONSE: if (rsp_ready[owner]) state <= IDLE;
                FLUSH_SCAN: begin
                    victim_set <= scan[SW:1];
                    victim_way <= scan[0];
                    victim_addr <= {tags[scan[SW:1]][scan[0]], scan[SW:1], {OFFSET{1'b0}}};
                    beat <= 0;
                    aw_sent <= 0;
                    w_sent <= 0;
                    if (valid[scan[SW:1]][scan[0]] && dirty[scan[SW:1]][scan[0]])
                        state <= EVICT;
                    else begin
                        valid[scan[SW:1]][scan[0]] <= 0;
                        if (scan == LAST_SCAN) state <= FLUSH_RESP;
                        else scan <= scan + 1'b1;
                    end
                end
                FLUSH_RESP: if (flush_done_ready) state <= IDLE;
                default: state <= IDLE;
            endcase
        end
    end

`ifdef CACHE_ASSERT
    reg past_valid;
    reg prev_awstall, prev_wstall, prev_arstall, prev_rspstall, prev_flushstall;
    reg [31:0] prev_awaddr, prev_wdata, prev_araddr, prev_rspdata;
    reg prev_wlast, prev_rsperror, prev_flusherror;
    reg [7:0] prev_rspid;
    reg [1:0] prev_rspvalid;
    integer as_set, as_way;
    always @(posedge clk) begin
        if (!rst_n) past_valid <= 0;
        else begin
            if (past_valid) begin
                if (prev_awstall) assert (awvalid && awaddr == prev_awaddr);
                if (prev_wstall) assert (wvalid && wdata == prev_wdata && wlast == prev_wlast);
                if (prev_arstall) assert (arvalid && araddr == prev_araddr);
                if (prev_rspstall) assert (rsp_valid == prev_rspvalid && rsp_data == prev_rspdata &&
                    rsp_id == prev_rspid && rsp_error == prev_rsperror);
                if (prev_flushstall) assert (flush_done && flush_error == prev_flusherror);
            end
            assert (!(valid[set_index][0] && valid[set_index][1] &&
                tags[set_index][0] == tags[set_index][1]));
            for (as_set=0; as_set<SETS; as_set=as_set+1)
                for (as_way=0; as_way<2; as_way=as_way+1)
                    assert (!dirty[as_set][as_way] || valid[as_set][as_way]);
            past_valid <= 1;
            prev_awstall <= awvalid && !awready;
            prev_wstall <= wvalid && !wready;
            prev_arstall <= arvalid && !arready;
            prev_rspstall <= |(rsp_valid & ~rsp_ready);
            prev_flushstall <= flush_done && !flush_done_ready;
            prev_awaddr <= awaddr;
            prev_wdata <= wdata;
            prev_wlast <= wlast;
            prev_araddr <= araddr;
            prev_rspvalid <= rsp_valid;
            prev_rspdata <= rsp_data;
            prev_rspid <= rsp_id;
            prev_rsperror <= rsp_error;
            prev_flusherror <= flush_error;
        end
    end
`endif
`ifdef FORMAL
    always @* if ($initstate) assume (!rst_n);
    always @(posedge clk) if (rst_n) begin
        cover (event_hit);
        cover (event_miss);
        cover (awvalid && awready);
        cover (wvalid && wready && wlast);
        cover (flush_done && flush_error);
        cover (rsp_valid != 0 && rsp_error);
    end
`endif
endmodule
