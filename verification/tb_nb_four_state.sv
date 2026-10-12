module tb_nb_four_state;
    reg clk=0;
    always #5 clk=~clk;
    reg rst_n=0;
    reg [1:0] req_valid=0, req_write=0, rsp_ready=3;
    reg [63:0] req_addr=0, req_data=0;
    reg [7:0] req_mask=0;
    reg [15:0] req_id=0;
    wire [1:0] req_ready,rsp_valid;
    wire [63:0] rsp_data;
    wire [15:0] rsp_id;
    wire [1:0] rsp_error;
    wire flush_ready,flush_done,flush_error;
    reg flush_valid=0,flush_done_ready=1;
    wire awvalid,wvalid,arvalid,bready,rready,wlast;
    wire [31:0] awaddr,araddr,wdata;
    reg awready=1,wready=1,arready=1,bvalid=0,rvalid=0;
    reg [31:0] rdata=0;
    reg [1:0] bresp=0,rresp=0;
    reg [1:0] bid=0,rid=0;reg rlast=0;
    integer count=0,read_count=0,write_count=0;
    reg read_pending=0,write_address=0;
    cache_nb dut (.clk(clk),.rst_n(rst_n),.req_valid(req_valid),.req_ready(req_ready),
        .req_addr(req_addr),.req_write(req_write),.req_data(req_data),.req_mask(req_mask),
        .req_id(req_id),.rsp_valid(rsp_valid),.rsp_ready(rsp_ready),.rsp_data(rsp_data),
        .rsp_id(rsp_id),.rsp_error(rsp_error),.flush_valid(flush_valid),
        .flush_ready(flush_ready),.flush_done(flush_done),.flush_done_ready(flush_done_ready),
        .flush_error(flush_error),.awvalid(awvalid),.awready(awready),.awaddr(awaddr),
        .wvalid(wvalid),.wready(wready),.wdata(wdata),.wlast(wlast),
        .bvalid(bvalid),.bready(bready),.bid(bid),.bresp(bresp),
        .arvalid(arvalid),.arready(arready),.araddr(araddr),
        .rvalid(rvalid),.rready(rready),.rdata(rdata),.rid(rid),.rresp(rresp),.rlast(rlast));
    always @(posedge clk) begin
        if(rst_n) begin
            if ((^{req_ready,rsp_valid,flush_ready,flush_done,awvalid,wvalid,arvalid,bready,rready}) === 1'bx)
                $fatal(1,"unknown control after reset req=%b rsp=%b flush=%b%b aw=%b w=%b ar=%b b=%b r=%b state=%b",
                    req_ready,rsp_valid,flush_ready,flush_done,awvalid,wvalid,arvalid,bready,rready,dut.phase);
            if (rsp_valid && ((^{rsp_data,rsp_id,rsp_error}) === 1'bx))
                $fatal(1,"unknown valid response");
            if (awvalid && ((^awaddr) === 1'bx)) $fatal(1,"unknown AW address");
            if (arvalid && ((^araddr) === 1'bx)) $fatal(1,"unknown AR address");
            if (wvalid && ((^{wdata,wlast}) === 1'bx)) $fatal(1,"unknown valid W data");
        end
        if(arvalid&&arready)begin read_pending<=1;read_count<=0;end
        if(rvalid&&rready)begin
            read_count<=read_count+1;
            if(rlast)read_pending<=0;
        end
        if(awvalid&&awready)write_address<=1;
        if(wvalid&&wready)begin
            if(write_count==0 && wdata!==32'h7856aa12)$fatal(1,"four-state dirty word mismatch");
            write_count<=write_count+1;
            if(wlast)begin
                if(write_count!=3)$fatal(1,"four-state W count");
                bvalid<=1;
            end
        end
        if(bvalid&&bready)begin
            if(!write_address)$fatal(1,"B without AW");
            bvalid<=0;
        end
        count<=count+1;
        if(count>300)$fatal(1,"four-state timeout");
    end
    always @(negedge clk)begin
        rvalid=read_pending;
        rdata=32'h78563412+32'(read_count);
        rlast=read_count==3;
    end
    task access(input bit wr,input [31:0] value,input [3:0] mask,input [31:0] expected);
        @(negedge clk);
        req_valid=1;req_write={1'b0,wr};req_data={32'b0,value};req_mask={4'b0,mask};req_id=16'h0037;
        do @(posedge clk); while(!req_ready[0]);
        @(negedge clk);req_valid=0;
        do @(posedge clk); while(!rsp_valid[0]);
        if(rsp_data[31:0]!==expected||rsp_id[7:0]!==8'h37||rsp_error!==0)$fatal(1,"four-state response mismatch");
    endtask
    initial begin
        repeat(3)@(negedge clk);rst_n=1;
        access(0,0,15,32'h78563412);
        access(1,32'h0000aa00,2,0);
        access(0,0,15,32'h7856aa12);
        @(negedge clk);flush_valid=1;
        do @(posedge clk);while(!flush_ready);
        @(negedge clk);flush_valid=0;
        do @(posedge clk);while(!flush_done);
        if(flush_error!==0||write_count!=4)$fatal(1,"four-state flush mismatch");
        $display("NB FOUR STATE PASS requests=3 dirty_flush_beats=4");
        $finish;
    end
endmodule
