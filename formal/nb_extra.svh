    reg nb_past_valid=0;
    reg [1:0] nb_pending;
    reg [7:0] nb_id[0:1];
    reg nb_bank[0:1];
    integer np;
    always @(posedge clk)begin
        nb_past_valid<=1;
        if(!nb_past_valid)assume(!rst_n);
        if(!rst_n)begin
            nb_pending<=0;nb_id[0]<=0;nb_id[1]<=0;nb_bank[0]<=0;nb_bank[1]<=0;
        end else begin
            assert(read_count[0]<WORDS&&read_count[1]<WORDS);
            for(np=0;np<2;np=np+1)begin
                if(req_valid[np]&&req_ready[np])begin
                    assert(!nb_pending[np]);nb_pending[np]<=1;
                    nb_id[np]<=req_id[8*np+:8];nb_bank[np]<=req_addr[32*np+OFFSET];
                end
                if(rsp_valid[np])begin
                    assert(nb_pending[np]);assert(rsp_id[8*np+:8]==nb_id[np]);
                    assert(bank_rsp_valid[nb_bank[np]][np]);
                end
                if(rsp_valid[np]&&rsp_ready[np])nb_pending[np]<=0;
            end
            if(nb_past_valid&&$past(rst_n)&&!protocol_error&&!$past(protocol_error))begin
                if($past(awvalid&&!awready))assert(awvalid&&$stable({awaddr,awid,awlen,awsize,awburst,awlock,awcache,awprot,awqos}));
                if($past(wvalid&&!wready))assert(wvalid&&$stable({wdata,wstrb,wlast}));
                if($past(arvalid&&!arready))assert(arvalid&&$stable({araddr,arid,arlen,arsize,arburst,arlock,arcache,arprot,arqos}));
                for(np=0;np<2;np=np+1)
                    if($past(rsp_valid[np]&&!rsp_ready[np]))
                        assert(rsp_valid[np]&&$stable({rsp_data[32*np+:32],rsp_id[8*np+:8],rsp_error[np]}));
                if($past(write_busy&&!(bvalid&&bready)))assert(write_busy&&$stable(write_owner));
                if($past(flush_done&&!flush_done_ready))assert(flush_done&&$stable(flush_error));
            end
            if(nb_past_valid&&$past(rst_n&&protocol_error))assert(protocol_error);
            if(protocol_error)assert(req_ready==0&&!awvalid&&!wvalid&&!arvalid&&!rready&&!bready);
            cover(read_outstanding==2'b11);
            cover(read_outstanding[0]&&bank_rsp_valid[1]!=0);
            cover(rsp_valid==2'b11);
            cover(protocol_error);
        end
    end
