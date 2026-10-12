module unknown_probe;
    reg [1:0] a=2'b10,b=2'b00;
    reg c=1,d=0,e=0,f=0,g=0,h=0,i=0;
    initial begin
        #1;
        $display("known concatenation isunknown=%b xor_unknown=%b bits=%b",
            $isunknown({a,b,c,d,e,f,g,h,i}),((^{a,b,c,d,e,f,g,h,i})===1'bx),{a,b,c,d,e,f,g,h,i});
        a=2'b1x;
        #1;
        if((^{a,b,c,d,e,f,g,h,i})!==1'bx)$fatal(1,"X checker missed planted X");
        $display("X CHECKER CONTROL PASS");
        $finish;
    end
endmodule
