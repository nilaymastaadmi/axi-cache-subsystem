#ifdef CORE_INTEGRATION
#include "Vrv32_cache_top.h"
using Top=Vrv32_cache_top;
#else
#include "Vcache_compare.h"
using Top=Vcache_compare;
#endif
#include "verilated.h"
#include <array>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct Request {uint32_t addr,data;unsigned mask,id;bool write;};
struct Read {bool active=false;uint32_t addr=0;unsigned count=0,beat=0;uint64_t ready=0;};
struct WBeat {uint32_t data;unsigned mask;bool last;};
class Compare {
public:
    Top d;
    std::vector<uint8_t> ref,mem;
    std::deque<Request> q[2];
    Request pending[2];bool active[2]={false,false};uint32_t expected[2]={0,0},response_data[2]={0,0};uint64_t start[2]={0,0};
    Read reads[2];std::deque<WBeat> writes;
    uint64_t cycle=0,accepted=0,completed=0,hits=0,misses=0,rb=0,wb=0,rt=0,wt=0,store_bytes=0,latency_sum=0,latency_max=0;
    bool aw=false,bpending=false,rhold=false,done=false;uint32_t wa=0;
    unsigned wcount=0,wlength=0,wid=0,rid=0,turn=0,latency;
    uint64_t btime=0,flush_start=0;
    explicit Compare(unsigned delay):ref(65536),mem(65536),latency(delay){
#ifdef CORE_INTEGRATION
        d.cpu_clk=0;d.cpu_rst_n=0;d.cpu_imem_data=0;d.cpu_dmem_rdata=0;
#endif
        for(unsigned i=0;i<mem.size();i++)ref[i]=mem[i]=uint8_t(i*43+(i>>5));
        d.rst_n=0;d.req_valid=d.flush_valid=d.rvalid=d.bvalid=0;
        d.rsp_ready=d.flush_done_ready=d.awready=d.wready=d.arready=0;
        for(unsigned i=0;i<3;i++){d.clk=0;d.eval();d.clk=1;d.eval();}d.rst_n=1;
    }
    void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message+" cycle="+std::to_string(cycle));}
    uint32_t word(const std::vector<uint8_t>& bytes,unsigned addr){
        require(addr+3<bytes.size(),"memory range");uint32_t value=0;
        for(unsigned i=0;i<4;i++)value|=uint32_t(bytes[addr+i])<<(8*i);return value;
    }
    void populate(const std::string& workload){
        for(unsigned i=0;i<2048;i++)for(unsigned p=0;p<2;p++){
            unsigned addr=0;
            if(workload=="locality")addr=p*16+(i%4)*4;
            else if(workload=="streaming")addr=(2*(i/4)+p)*16+(i%4)*4;
            else if(workload=="conflict")addr=((i%3)*4+p)*16;
            else if(workload=="same_bank")addr=p*32+(i%4)*4;
            else if(workload=="same_bank_streaming")addr=(2*(i/4)+p)*32+(i%4)*4;
            else throw std::runtime_error("unknown workload");
            q[p].push_back({addr,uint32_t(0x98761234u+i*71+p*0x12345678u),1+i%15,unsigned(i&255),i%4==0});
        }
    }
    void step(){
        require(cycle<1000000,"timeout");d.clk=0;
        d.req_valid=d.req_addr=d.req_data=d.req_write=d.req_mask=d.req_id=0;
        for(unsigned p=0;p<2;p++)if(!active[p]&&!q[p].empty()){
            auto a=q[p].front();d.req_valid|=1u<<p;d.req_addr|=uint64_t(a.addr)<<(32*p);
            d.req_data|=uint64_t(a.data)<<(32*p);d.req_write|=unsigned(a.write)<<p;
            d.req_mask|=a.mask<<(4*p);d.req_id|=a.id<<(8*p);
        }
        d.rsp_ready=cycle%5?3:0;d.awready=cycle%4!=0;d.wready=(cycle+1)%4!=0;d.arready=(cycle+2)%4!=0;
        d.flush_done_ready=1;d.bvalid=bpending&&cycle>=btime;d.bid=wid;d.bresp=0;
        if(!rhold){
            unsigned candidate=turn;
            if(!reads[candidate].active||cycle<reads[candidate].ready)candidate^=1;
            if(reads[candidate].active&&cycle>=reads[candidate].ready){rid=candidate;rhold=true;}
        }
        d.rvalid=rhold;d.rid=rid;d.rresp=0;
        d.rdata=rhold?word(mem,reads[rid].addr+4*reads[rid].beat):0;
        d.rlast=rhold&&reads[rid].beat+1==reads[rid].count;
        d.eval();require(!d.protocol_error,"protocol fault");
        hits+=unsigned(d.event_hit&1)+unsigned((d.event_hit>>1)&1);
        misses+=unsigned(d.event_miss&1)+unsigned((d.event_miss>>1)&1);
        for(unsigned p=0;p<2;p++){
            if((d.req_valid&d.req_ready)&(1u<<p)){
                require(!active[p],"double accept");auto a=q[p].front();q[p].pop_front();pending[p]=a;
                expected[p]=a.write?0:word(ref,a.addr);start[p]=cycle;active[p]=true;accepted++;
                if(a.write)for(unsigned i=0;i<4;i++)if(a.mask&(1u<<i))ref[a.addr+i]=uint8_t(a.data>>(8*i));
            }
            if((d.rsp_valid&d.rsp_ready)&(1u<<p)){
                require(active[p],"unsolicited response");require(((d.rsp_id>>(8*p))&255)==pending[p].id,"response identity");
                require(!(d.rsp_error&(1u<<p)),"response error");
                require(uint32_t(d.rsp_data>>(32*p))==expected[p],"architectural bytes");
                response_data[p]=uint32_t(d.rsp_data>>(32*p));
                uint64_t elapsed=cycle-start[p]+1;latency_sum+=elapsed;if(elapsed>latency_max)latency_max=elapsed;
                active[p]=false;completed++;
            }
        }
        if(d.awvalid&&d.awready){
            require(!aw&&!bpending,"AW serialization");require(d.awid<2&&d.awsize==2&&d.awburst==1,"AW profile");
            require(d.awaddr%4==0&&(d.awaddr%4096)+4*(d.awlen+1)<=4096,"AW address");
            aw=true;wa=d.awaddr;wid=d.awid;wlength=d.awlen+1;wcount=0;wb++;
        }
        if(d.wvalid&&d.wready){writes.push_back({d.wdata,d.wstrb,bool(d.wlast)});wt++;}
        while(aw&&!writes.empty()){
            auto beat=writes.front();writes.pop_front();require(wcount<wlength,"excess W");
            require(beat.last==(wcount+1==wlength),"WLAST");
            for(unsigned lane=0;lane<4;lane++)if(beat.mask&(1u<<lane)){
                auto value=uint8_t(beat.data>>(8*lane));require(value==ref[wa+4*wcount+lane],"dirty write byte");
                mem[wa+4*wcount+lane]=value;store_bytes++;
            }
            wcount++;if(beat.last){aw=false;bpending=true;btime=cycle+latency;}
        }
        if(d.bvalid&&d.bready){require(bpending,"unsolicited B");bpending=false;}
        if(d.arvalid&&d.arready){
            require(d.arid<2&&!reads[d.arid].active&&d.arsize==2&&d.arburst==1,"AR slot/profile");
            require(d.araddr%4==0&&(d.araddr%4096)+4*(d.arlen+1)<=4096,"AR address");
            reads[d.arid]={true,d.araddr,unsigned(d.arlen)+1,0,cycle+latency};rb++;
        }
        if(d.rvalid&&d.rready){
            rt++;reads[rid].beat++;rhold=false;turn=rid^1;
            if(reads[rid].beat==reads[rid].count)reads[rid].active=false;
        }
        bool flush_accepted=d.flush_valid&&d.flush_ready;
        if(d.flush_done&&d.flush_done_ready){require(!d.flush_error,"flush error");done=true;}
        d.clk=1;d.eval();cycle++;if(flush_accepted)d.flush_valid=0;
    }
    void run(const std::string& workload,const std::string& path){
        populate(workload);
        while(!q[0].empty()||!q[1].empty()||active[0]||active[1])step();
        flush_start=cycle;d.flush_valid=1;while(!done)step();
        require(accepted==4096&&completed==accepted,"request conservation");require(ref==mem,"final memory equality");
        require(!reads[0].active&&!reads[1].active&&!aw&&!bpending&&writes.empty(),"unfinished AXI");
        std::ofstream image(path+".memory",std::ios::binary);image.write(reinterpret_cast<const char*>(mem.data()),mem.size());
        image.close();require(bool(image),"memory image write");
        std::ofstream out(path);require(bool(out),"output open");
        out<<"{\"status\":\"PASS\",\"workload\":\""<<workload<<"\",\"latency\":"<<latency
           <<",\"accepted\":"<<accepted<<",\"completed\":"<<completed<<",\"cycles\":"<<cycle
           <<",\"flush_cycles\":"<<cycle-flush_start<<",\"latency_sum\":"<<latency_sum<<",\"latency_max\":"<<latency_max
           <<",\"hits\":"<<hits<<",\"misses\":"<<misses<<",\"read_bursts\":"<<rb<<",\"write_bursts\":"<<wb
           <<",\"read_beats\":"<<rt<<",\"write_beats\":"<<wt<<",\"write_enabled_bytes\":"<<store_bytes<<"}\n";
        out.close();require(bool(out),"output write");
        std::cout<<"COMPARE PASS requests="<<completed<<" cycles="<<cycle<<"\n";
    }
};
#ifndef CORE_INTEGRATION
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);if(argc!=4)return 2;
    try{Compare c(std::stoul(argv[2]));c.run(argv[1],argv[3]);return 0;}
    catch(const std::exception& e){std::cerr<<"COMPARE FAIL "<<e.what()<<"\n";return 1;}
}
#endif
