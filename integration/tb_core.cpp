#define CORE_INTEGRATION
#include "../measurement/tb_compare.cpp"

uint32_t addi(unsigned rd,unsigned rs,int imm){return (uint32_t(imm)&4095)<<20|rs<<15|rd<<7|0x13;}
uint32_t store(unsigned rs2,unsigned rs1,int imm,unsigned kind){
    unsigned x=unsigned(imm)&4095;return (x>>5)<<25|rs2<<20|rs1<<15|kind<<12|(x&31)<<7|0x23;
}
uint32_t load(unsigned rd,unsigned rs1,int imm,unsigned kind){return (unsigned(imm)&4095)<<20|rs1<<15|kind<<12|rd<<7|3;}
uint32_t branch(unsigned rs1,unsigned rs2,int imm){
    unsigned x=unsigned(imm)&8191;return ((x>>12)&1)<<31|((x>>5)&63)<<25|rs2<<20|rs1<<15|1<<12|
        ((x>>1)&15)<<8|((x>>11)&1)<<7|0x63;
}
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);if(argc!=2)return 2;
    try{
        Compare c(10);auto& d=c.d;
        std::vector<uint32_t> program={addi(1,0,1024),addi(2,0,127),store(2,1,0,2),load(3,1,0,2),
            store(2,1,1,0),load(4,1,1,4),addi(5,0,-1),store(5,1,2,1),load(6,1,0,2),
            uint32_t(4<<20|3<<15|7<<7|0x33),addi(8,0,64),store(7,1,0,2),load(9,1,0,2),
            addi(1,1,16),addi(7,7,1),addi(8,8,-1),branch(8,0,-20),0x73};
        for(unsigned i=0;i<3;i++){d.cpu_clk=0;d.eval();d.cpu_clk=1;d.eval();}d.cpu_clk=0;d.cpu_rst_n=1;d.eval();
        std::ofstream trace(std::string(argv[1])+".trace");c.require(bool(trace),"trace open");
        unsigned retired=0,iterations=0;bool stopped=false;
        while(!stopped){
            uint32_t pc=d.cpu_imem_addr;c.require(pc%4==0&&pc/4<program.size(),"fetch range");
            auto insn=program[pc/4];d.cpu_imem_data=insn;d.eval();
            unsigned opcode=insn&127;
            if(opcode==3||opcode==0x23){
                c.require(d.cpu_dmem_addr>=1024&&d.cpu_dmem_addr<4096,"data RAM map (MMIO excluded)");
                unsigned port=retired%2;
                c.q[port].push_back({d.cpu_dmem_addr,d.cpu_dmem_wdata,unsigned(d.cpu_dmem_wstrb),unsigned(retired&255),opcode==0x23});
                uint64_t before=c.completed;while(c.completed==before)c.step();
                d.cpu_dmem_rdata=c.response_data[port];d.eval();
            }
            auto value=d.cpu_retire_val;
            if(pc==12)c.require(value==127&&d.cpu_retire_rd==3,"LW initial");
            if(pc==20)c.require(value==127&&d.cpu_retire_rd==4,"LBU byte write");
            if(pc==32)c.require(value==0xffff7f7fu&&d.cpu_retire_rd==6,"LW halfword lanes");
            if(pc==48){c.require(value==254+iterations&&d.cpu_retire_rd==9,"loop load");iterations++;}
            trace<<std::hex<<pc<<" "<<insn<<" "<<unsigned(d.cpu_retire_we)<<" "<<unsigned(d.cpu_retire_rd)<<" "<<value<<"\n";
            retired++;stopped=insn==0x73;
            d.cpu_clk=1;d.eval();d.cpu_clk=0;d.eval();c.step();
            c.require(retired<1000,"retire limit");
        }
        c.require(d.cpu_halted&&iterations==64&&retired==396,"program stop/loop count");
        c.flush_start=c.cycle;d.flush_valid=1;while(!c.done)c.step();
        c.require(c.accepted==134&&c.completed==134&&c.mem==c.ref,"cache integration memory/conservation");
        for(unsigned i=0;i<64;i++)c.require(c.word(c.mem,1024+16*i)==254+i,"expected stored loop word");
        trace.close();c.require(bool(trace),"trace close");
        std::ofstream image(std::string(argv[1])+".memory",std::ios::binary);
        image.write(reinterpret_cast<const char*>(c.mem.data()),c.mem.size());image.close();c.require(bool(image),"image write");
        std::ofstream out(argv[1]);out<<"{\"status\":\"PASS\",\"retired\":"<<retired<<",\"memory_requests\":"<<c.completed
          <<",\"cycles\":"<<c.cycle<<",\"iterations\":"<<iterations<<",\"flush_cycles\":"<<c.cycle-c.flush_start<<"}\n";
        out.close();c.require(bool(out),"summary write");
        std::cout<<"CORE CACHE PASS retired="<<retired<<" requests="<<c.completed<<" cycles="<<c.cycle<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"CORE CACHE FAIL "<<e.what()<<"\n";return 1;}
}
