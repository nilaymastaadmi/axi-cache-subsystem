    reg formal_pending;
    reg formal_owner;
    reg [7:0] formal_id;
    integer fs,fw;
    always @(posedge clk)begin
        if(!rst_n)begin formal_pending<=0;formal_owner<=0;formal_id<=0;end
        else begin
            if (|(req_valid&req_ready))begin
                assert(!formal_pending);
                formal_pending<=1;
                formal_owner<=req_ready[1];
                formal_id<=req_id[req_ready[1]*8+:8];
            end
            if(rsp_valid!=0)begin
                assert(formal_pending);
                assert(rsp_valid==(formal_owner?2'b10:2'b01));
                assert(rsp_id==formal_id);
            end
            if(|(rsp_valid&rsp_ready))formal_pending<=0;
            if(past_valid&&$past(rst_n))begin
                for(fs=0;fs<SETS;fs=fs+1)for(fw=0;fw<2;fw=fw+1)begin
                    if($past(dirty[fs][fw])&&!dirty[fs][fw])
                        assert($past(state==WB_RESP&&bvalid&&bresp==0&&bid==0&&
                            victim_set==SW'(fs)&&victim_way==1'(fw)));
                    if($past(valid[fs][fw]&&dirty[fs][fw])&&!valid[fs][fw])
                        assert($past(state==WB_RESP&&bvalid&&bresp==0&&bid==0&&maintenance&&
                            victim_set==SW'(fs)&&victim_way==1'(fw)));
                end
            end
            cover(past_valid&&$past(rst_n&&!maintenance)&&$past(dirty[0][0])&&!dirty[0][0]);
            cover(past_valid&&$past(rst_n)&&$past(valid[0][0]&&dirty[0][0])&&!valid[0][0]);
        end
    end
