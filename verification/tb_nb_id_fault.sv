module tb_nb_id_fault # (parameter integer BAD_ID=1, parameter integer EXTRA_BEAT=0);
    reg clk=0;always #5 clk=~clk;
    reg rst_n=0;reg [1:0] req_valid=0;
    wire [1:0] req_ready,rsp_valid;
    wire awvalid,wvalid,arvalid,rready,bready,protocol_error;
    wire [1:0] arid;
    reg rvalid=0,rlast=0;
    integer cycles=0,completed=0,beat;
    cache_nb dut(.clk(clk),.rst_n(rst_n),.req_valid(req_valid),.req_ready(req_ready),
      .req_addr(64'b0),.req_write(2'b0),.req_data(64'b0),.req_mask(8'hff),.req_id(16'h0012),
      .rsp_valid(rsp_valid),.rsp_ready(2'b11),.flush_valid(1'b0),.flush_done_ready(1'b1),
      .awvalid(awvalid),.awready(1'b1),.wvalid(wvalid),.wready(1'b1),
      .bvalid(1'b0),.bid(2'b0),.bresp(2'b0),.bready(bready),.arvalid(arvalid),.arready(1'b1),.arid(arid),
      .rvalid(rvalid),.rready(rready),.rid(2'(BAD_ID)),.rresp(2'b0),.rdata(32'h12345678),.rlast(rlast),
      .protocol_error(protocol_error));
    always @(posedge clk)begin
        cycles<=cycles+1;if(cycles>100)$fatal(1,"ID fault timeout");
        if(rst_n&&rsp_valid[0])completed<=completed+1;
    end
    initial begin
        repeat(3)@(negedge clk);rst_n=1;req_valid=1;
        do @(posedge clk);while(!req_ready[0]);@(negedge clk);req_valid=0;
        do @(posedge clk);while(!arvalid);
        if(arid!==0)$fatal(1,"expected live bank0 ID");
        if(EXTRA_BEAT)begin
            for(beat=0;beat<4;beat=beat+1)begin
                @(negedge clk);rvalid=1;rlast=beat==3;
                do @(posedge clk);while(!rready);
                @(negedge clk);rvalid=0;rlast=0;
            end
            wait(completed==1);
            repeat(3)@(negedge clk);
            if(protocol_error!==0)$fatal(1,"legal burst before excess beat");
        end
        @(negedge clk);rvalid=1;
        do @(posedge clk);while(!rready);
        @(negedge clk);rvalid=0;
        repeat(8)begin
            @(posedge clk);
            if(protocol_error!==1||req_ready!==0||awvalid!==0||wvalid!==0||arvalid!==0||rready!==0||bready!==0||rsp_valid!==0)
                $fatal(1,"ID fault containment");
        end
        @(negedge clk);rst_n=0;repeat(2)@(negedge clk);rst_n=1;
        @(posedge clk);if(protocol_error!==0)$fatal(1,"ID fault reset clear");
        if(EXTRA_BEAT)begin
            if(completed!=1)$fatal(1,"excess beat duplicate response");
            $display("NB EXCESS R PASS bad_id=0 accepted=1 completed=1 cancelled_by_reset=0");
        end else $display("NB ID FAULT PASS bad_id=%0d accepted=1 cancelled_by_reset=1",BAD_ID);
        $finish;
    end
endmodule
