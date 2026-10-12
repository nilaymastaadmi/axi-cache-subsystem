#include "Vcache_nb.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef TEST_SETS
#define TEST_SETS 4
#endif
#ifndef TEST_WORDS
#define TEST_WORDS 4
#endif
constexpr unsigned SETS=TEST_SETS, WORDS=TEST_WORDS, LINE=4*WORDS, MEM=65536;
struct Req {uint32_t addr,data;unsigned mask,id;bool write,error;};
struct Pending {Req req;uint32_t expected;std::array<uint8_t,4> old;};
struct Read {bool active=false;uint32_t addr=0;unsigned beat=0;int error=-1;int last_mode=0;};
class Bench {
public:
    Vcache_nb d;
    std::vector<uint8_t> ref,mem;
    std::mt19937 bus;
    std::deque<Req> queues[2];
    Pending pending[2];
    bool active[2]={false,false};
    Read reads[2];
    std::map<std::string,uint64_t> bins;
    std::deque<uint32_t> wqueue;
    uint64_t cycle=0,accepted=0,completed=0,read_bursts=0,write_bursts=0,rbeats=0,wbeats=0;
    uint64_t cancelled=0,aborted_rbeats=0,aborted_wbeats=0,orphan_wbeats=0,invalid_rbeats=0;
    bool aw=false,bpending=false,bhold=false,wlast=false,rhold=false;
    uint32_t wa=0;unsigned wid=0,wcount=0,rselect=0;
    int next_read_error[2]={-1,-1},next_write_error=0,write_error=0;
    int next_last_mode[2]={0,0};
    bool inject_invalid_rid=false,allow_protocol_fault=false;
    bool hold_bank0=false,force_bank1=false,hold_reads=false,alternate_reads=false,hold_b=false;
    unsigned read_turn=0;
    unsigned response_hold[2]={0,0};
    unsigned bank_turn[2]={0,0};
    unsigned max_reads=0;
    bool awstall=false,wstall=false,arstall=false;
    uint32_t sawaddr=0,swdata=0,saraddr=0;
    unsigned sawid=0,sarid=0,swlast=0;
    bool rspstall[2]={false,false};
    uint32_t srspdata[2]={0,0};unsigned srspid[2]={0,0},srsperror[2]={0,0};
    unsigned write_age=0,order_mode=0;
    bool inject_bad_bid=false,bad_bid=false,inject_live_bad_bid=false,live_bad_bid=false;
    explicit Bench(unsigned seed):ref(MEM),mem(MEM),bus(seed^0x12345678) {
        for(unsigned i=0;i<MEM;i++)ref[i]=mem[i]=uint8_t(i*71+(i>>4));
        d.rst_n=0;d.req_valid=0;d.flush_valid=0;d.rsp_ready=0;d.flush_done_ready=0;
        d.awready=d.wready=d.arready=d.bvalid=d.rvalid=0;
        for(int i=0;i<3;i++){d.clk=0;d.eval();d.clk=1;d.eval();}
        d.rst_n=1;
    }
    void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why+" cycle="+std::to_string(cycle));}
    uint32_t word(const std::vector<uint8_t>& m,unsigned a){require(a+3<MEM,"address range");
        uint32_t value=0;for(unsigned i=0;i<4;i++)value|=uint32_t(m[a+i])<<(8*i);return value;}
    void step(){
        require(cycle<200000,"timeout");
        d.clk=0;
        d.req_valid=d.req_addr=d.req_data=d.req_write=d.req_mask=d.req_id=0;
        for(unsigned p=0;p<2;p++)if(!active[p]&&!queues[p].empty()){
            auto q=queues[p].front();d.req_valid|=1u<<p;d.req_addr|=uint64_t(q.addr)<<(32*p);
            d.req_data|=uint64_t(q.data)<<(32*p);d.req_write|=unsigned(q.write)<<p;
            d.req_mask|=q.mask<<(4*p);d.req_id|=q.id<<(8*p);
        }
        d.rsp_ready=0;
        for(unsigned p=0;p<2;p++){
            if(response_hold[p])--response_hold[p];else if(bus()%3)d.rsp_ready|=1u<<p;
        }
        d.flush_done_ready=1;
        d.awready=bus()%3!=0;d.wready=bus()%3!=0;d.arready=bus()%3!=0;
        if(order_mode==1){d.awready=write_age>WORDS+8;d.wready=1;}
        if(order_mode==2){d.awready=1;d.wready=write_age>WORDS+8;}
        if(bpending&&!bhold&&bus()%3==0)bhold=true;
        d.bvalid=bhold&&!hold_b;d.bid=bad_bid?(live_bad_bid?1-wid:2):wid;d.bresp=write_error;
        if(!rhold){
            unsigned candidate=alternate_reads?read_turn:(force_bank1?1:bus()%2);
            if(!reads[candidate].active)candidate^=1;
            if(!hold_reads&&reads[candidate].active&&!(hold_bank0&&candidate==0)&&bus()%3!=0){rhold=true;rselect=candidate;}
        }
        d.rvalid=rhold||inject_invalid_rid;d.rid=inject_invalid_rid?2:rselect;
        d.rdata=rhold?word(mem,reads[rselect].addr+4*reads[rselect].beat):0;
        d.rresp=rhold&&int(reads[rselect].beat)==reads[rselect].error?2:0;
        d.rlast=rhold&&reads[rselect].beat==WORDS-1;
        if(rhold&&reads[rselect].last_mode==1)d.rlast=reads[rselect].beat==0;
        if(rhold&&reads[rselect].last_mode==2)d.rlast=0;
        d.eval();
        require(!d.protocol_error||allow_protocol_fault,"unexpected protocol fault");
        if(d.protocol_error)require(!d.req_ready&&!d.awvalid&&!d.wvalid&&!d.arvalid&&!d.rready&&!d.bready,
                                   "sticky protocol fault admission/AXI containment");
        require(!awstall||(d.awvalid&&d.awaddr==sawaddr&&d.awid==sawid),"AW stability");
        require(!wstall||(d.wvalid&&d.wdata==swdata&&d.wlast==swlast),"W stability");
        require(!arstall||(d.arvalid&&d.araddr==saraddr&&d.arid==sarid),"AR stability");
        awstall=d.awvalid&&!d.awready;wstall=d.wvalid&&!d.wready;arstall=d.arvalid&&!d.arready;
        sawaddr=d.awaddr;sawid=d.awid;swdata=d.wdata;swlast=d.wlast;saraddr=d.araddr;sarid=d.arid;
        if(awstall)bins["AW_stall"]++;if(wstall)bins["W_stall"]++;if(arstall)bins["AR_stall"]++;
        for(unsigned p=0;p<2;p++){
            uint32_t data=uint32_t(d.rsp_data>>(32*p));unsigned id=(d.rsp_id>>(8*p))&255,error=(d.rsp_error>>p)&1;
            require(!rspstall[p]||((d.rsp_valid&(1u<<p))&&data==srspdata[p]&&id==srspid[p]&&error==srsperror[p]),"response stability");
            rspstall[p]=(d.rsp_valid&(1u<<p))&&!(d.rsp_ready&(1u<<p));
            if(rspstall[p])bins["response_stall"]++;
            srspdata[p]=data;srspid[p]=id;srsperror[p]=error;
            if((d.req_valid&d.req_ready)&(1u<<p)){
                require(!active[p],"duplicate acceptance");auto q=queues[p].front();queues[p].pop_front();
                unsigned bank=(q.addr/LINE)%2;
                if(d.req_valid==3&&unsigned((d.req_addr>>(32+2+__builtin_ctz(WORDS)))&1)==unsigned((d.req_addr>>(2+__builtin_ctz(WORDS)))&1)){
                    require(p==bank_turn[bank],"bank round-robin contested grant");bins["bank"+std::to_string(bank)+"_contested_RR"]++;
                }
                bank_turn[bank]=1-p;
                pending[p].req=q;pending[p].expected=q.write?0:word(ref,q.addr&~3u);
                for(unsigned i=0;i<4;i++)pending[p].old[i]=ref[q.addr+i];
                if(q.write&&q.addr%4==0)for(unsigned i=0;i<4;i++)if(q.mask&(1u<<i))ref[q.addr+i]=uint8_t(q.data>>(8*i));
                active[p]=true;accepted++;bins["requester"+std::to_string(p)]++;
                if(q.write)bins["mask"+std::to_string(q.mask)]++;
            }
            if((d.rsp_valid&d.rsp_ready)&(1u<<p)){
                require(active[p],"duplicate/unsolicited response");auto q=pending[p];
                require(id==q.req.id,"response identity");require(bool(error)==q.req.error,"response error");
                if(!error)require(data==q.expected,"architectural response bytes");
                else {for(unsigned i=0;i<4;i++)if(q.req.write)ref[q.req.addr+i]=q.old[i];bins["request_error"]++;}
                active[p]=false;completed++;
                if(reads[0].active&&((q.req.addr/LINE)%2)==1)bins["bank1_completion_under_bank0_miss"]++;
            }
        }
        if(d.awvalid||d.wvalid)write_age++;
        if(d.awvalid&&d.awready){
            require(!aw&&!bpending,"write serialization");profile(true);aw=true;wa=d.awaddr;wid=d.awid;wcount=0;
            write_error=next_write_error;next_write_error=0;bad_bid=inject_bad_bid;inject_bad_bid=false;
            live_bad_bid=inject_live_bad_bid;inject_live_bad_bid=false;
            bins[wqueue.empty()?"AW_before_W":"W_before_AW"]++;write_bursts++;
        }
        if(d.wvalid&&d.wready){require(!wlast,"W after LAST");require(d.wstrb==15,"WSTRB");
            wqueue.push_back(d.wdata);wlast=d.wlast;wbeats++;}
        while(aw&&!wqueue.empty()){
            require(wcount<WORDS,"excess write beats");auto value=wqueue.front();wqueue.pop_front();
            require(value==word(ref,wa+4*wcount),"dirty byte integrity");
            if(!write_error&&!bad_bid)
                for(unsigned i=0;i<4;i++)mem[wa+4*wcount+i]=uint8_t(value>>(8*i));
            else bins["failed_writeback_uncommitted_beats"]++;
            wcount++;
        }
        if(aw&&wlast&&wqueue.empty()){require(wcount==WORDS,"WLAST");aw=false;bpending=true;write_age=0;}
        if(d.bvalid&&d.bready){require(bpending,"unexpected B");bpending=bhold=wlast=false;wcount=0;}
        if(d.arvalid&&d.arready){
            profile(false);unsigned id=d.arid;require(id<2&&!reads[id].active,"AR slot collision");
            require((!aw&&!bpending)||id!=wid,"refill before own dirty response");
            reads[id]={true,d.araddr,0,next_read_error[id],next_last_mode[id]};
            next_read_error[id]=-1;next_last_mode[id]=0;read_bursts++;
        }
        unsigned outstanding=unsigned(reads[0].active)+unsigned(reads[1].active);
        if(outstanding>max_reads)max_reads=outstanding;
        if(outstanding==2)bins["two_outstanding_read_bursts"]++;
        if(d.rvalid&&d.rready){
            if(inject_invalid_rid){invalid_rbeats++;inject_invalid_rid=false;bins["invalid_RID_containment"]++;}
            else {
            require(reads[rselect].active,"R routing");rbeats++;reads[rselect].beat++;rhold=false;
            if(alternate_reads){read_turn=1-rselect;if(reads[1-rselect].active)bins["interleaved_R_beats"]++;}
            if(d.rresp)bins["refill_error"]++;
            if(reads[rselect].beat==WORDS){
                if(rselect==1&&reads[0].active)bins["bank1_refill_before_bank0"]++;
                reads[rselect].active=false;
            }
            }
        }
        d.clk=1;d.eval();cycle++;
    }
    void profile(bool wr){
        unsigned addr=wr?d.awaddr:d.araddr,id=wr?d.awid:d.arid;
        require(addr%LINE==0&&(addr%4096)+LINE<=4096,"burst address");
        require(id==(addr/LINE)%2,"bank address reconstruction/ID");
        require((wr?d.awlen:d.arlen)==WORDS-1&&(wr?d.awsize:d.arsize)==2&&(wr?d.awburst:d.arburst)==1,"burst profile");
        require((wr?d.awlock:d.arlock)==0&&(wr?d.awcache:d.arcache)==0&&
                (wr?d.awprot:d.arprot)==0&&(wr?d.awqos:d.arqos)==0,"burst attributes");
    }
    void enqueue(unsigned port,Req req){queues[port].push_back(req);}
    void reset(const std::string& witness){
        for(unsigned p=0;p<2;p++){
            if(active[p])cancelled++;
            if(reads[p].active)aborted_rbeats+=WORDS-reads[p].beat;
            active[p]=false;reads[p]={};queues[p].clear();rspstall[p]=false;
            next_read_error[p]=-1;next_last_mode[p]=0;response_hold[p]=0;
            bank_turn[p]=0;
        }
        if(aw)aborted_wbeats+=WORDS-wcount-wqueue.size();
        else if(!bpending)orphan_wbeats+=wqueue.size();
        // Coordinated destructive reset restores architecture to backing bytes.
        ref=mem;wqueue.clear();aw=bpending=bhold=wlast=rhold=false;wcount=0;
        awstall=wstall=arstall=false;write_age=0;order_mode=0;
        hold_bank0=force_bank1=hold_reads=alternate_reads=hold_b=inject_invalid_rid=allow_protocol_fault=false;
        next_write_error=write_error=0;inject_bad_bid=bad_bid=false;
        inject_live_bad_bid=live_bad_bid=false;
        d.rst_n=0;d.req_valid=d.rvalid=d.bvalid=d.flush_valid=0;
        d.awready=d.wready=d.arready=0;
        for(unsigned i=0;i<3;i++){d.clk=0;d.eval();d.clk=1;d.eval();}
        d.rst_n=1;step();bins[witness]++;
    }
    void drain(){while(active[0]||active[1]||!queues[0].empty()||!queues[1].empty())step();step();}
    void access(unsigned p,uint32_t a,bool w=false,uint32_t value=0,unsigned mask=15,bool err=false){
        enqueue(p,{a,value,mask,unsigned((accepted*23+7)&255),w,err});drain();}
    void flush(bool error=false){
        d.flush_valid=1;
        while(!d.flush_ready)step();step();d.flush_valid=0;
        while(!d.flush_done)step();require(bool(d.flush_error)==error,"flush error status");
        step();if(!error)require(ref==mem,"flushed backing byte map");bins[error?"flush_error":"flush_success"]++;
    }
    void directed(){
        // Populate bank 1, then force its hit response ahead of bank 0 refill.
        access(1,LINE);hold_bank0=true;auto hum_reads=read_bursts;
        enqueue(0,{0,0,15,11,false,false});enqueue(1,{LINE,0,15,22,false,false});
        while(!reads[0].active)step();while(active[1]||!queues[1].empty())step();
        require(active[0]&&reads[0].active&&read_bursts==hum_reads+1,"hit-under-miss requires no bank1 refill");bins["hit_under_miss"]++;
        hold_bank0=false;drain();flush();
        for(unsigned bank=0;bank<2;bank++)for(unsigned round=0;round<2;round++){
            enqueue(0,{bank*LINE,0,15,107,false,false});enqueue(1,{(bank+2)*LINE,0,15,108,false,false});drain();
        }flush();
        hold_bank0=true;force_bank1=true;
        enqueue(0,{0,0,15,33,false,false});enqueue(1,{LINE,0,15,44,false,false});
        while(active[1]||!queues[1].empty())step();
        require(reads[0].active&&active[0],"out-of-order refill witness");
        hold_bank0=false;force_bank1=false;drain();flush();
        // Whole-bank reservation is observable even for another set.
        hold_bank0=true;enqueue(0,{0,0,15,55,false,false});
        while(!reads[0].active)step();enqueue(1,{2*LINE,0,15,66,false,false});
        auto before=accepted;for(unsigned i=0;i<12;i++)step();
        require(accepted==before,"same-bank reservation");bins["same_bank_stall"]++;
        hold_bank0=false;drain();flush();
        for(unsigned mask=0;mask<16;mask++){
            access(mask%2,mask%2*LINE,true,0x98765432u^mask,mask);access(1-mask%2,mask%2*LINE);
        }
        flush();
        for(unsigned mode=1;mode<=2;mode++){
            order_mode=mode;access(0,0,true,0xabcdef01);access(1,LINE,true,0x12345678);
            enqueue(0,{SETS*LINE,0xf1f2f3f4,15,77,true,false});enqueue(1,{(SETS+1)*LINE,0xa1a2a3a4,15,88,true,false});
            drain();enqueue(0,{2*SETS*LINE,0,15,89,false,false});enqueue(1,{(2*SETS+1)*LINE,0,15,90,false,false});drain();flush();
        }
        order_mode=0;
        response_hold[0]=40;enqueue(0,{0,0,15,99,false,false});enqueue(1,{LINE,0,15,100,false,false});drain();flush();
        for(unsigned bank=0;bank<2;bank++){
            for(unsigned beat=0;beat<WORDS;beat++){
                next_read_error[bank]=int(beat);hold_reads=true;alternate_reads=true;read_turn=bank;
                enqueue(bank,{1024+bank*LINE,0,15,121,false,true});
                enqueue(1-bank,{1024+(1-bank)*LINE,0,15,122,false,false});
                while(!reads[0].active||!reads[1].active)step();hold_reads=false;auto interleaved=bins["interleaved_R_beats"];
                drain();require(bins["interleaved_R_beats"]>interleaved,"R error with interleaved independent burst");alternate_reads=false;
                bins["bank"+std::to_string(bank)+"_interleaved_Rerror_beat"+std::to_string(beat)]++;
                access(1-bank,1024+bank*LINE);flush();bins["bank"+std::to_string(bank)+"_Rerror_beat"+std::to_string(beat)]++;
            }
            for(int mode=1;mode<=2;mode++){
                next_last_mode[bank]=mode;hold_reads=true;alternate_reads=true;read_turn=bank;
                enqueue(bank,{1024+bank*LINE,0,15,123,false,true});
                enqueue(1-bank,{1024+(1-bank)*LINE,0,15,124,false,false});
                while(!reads[0].active||!reads[1].active)step();hold_reads=false;auto interleaved=bins["interleaved_R_beats"];
                drain();require(bins["interleaved_R_beats"]>interleaved,"RLAST error with interleaved independent burst");alternate_reads=false;
                bins["bank"+std::to_string(bank)+"_interleaved_RLAST_mode"+std::to_string(mode)]++;
                access(1-bank,1024+bank*LINE);flush();bins["bank"+std::to_string(bank)+"_RLAST_mode"+std::to_string(mode)]++;
            }
        }
        // Failed request-path eviction cannot refill or discard its dirty victim;
        // the independent bank completes a proven hit while B is held.
        for(unsigned bank=0;bank<2;bank++)for(unsigned badid=0;badid<3;badid++){
            access(bank,bank*LINE,true,0xdeadbeefu^bank^((badid+1)*0x01010101u));
            access(bank,(SETS+bank)*LINE,true,0x12345678u^bank^((badid+1)*0x01010101u));
            access(1-bank,(1-bank)*LINE);auto before_reads=read_bursts;
            next_write_error=badid?0:2;inject_bad_bid=badid;hold_b=true;
            inject_live_bad_bid=badid==2;
            enqueue(bank,{(2*SETS+bank)*LINE,0,15,125,false,true});
            while(!bpending)step();enqueue(1-bank,{(1-bank)*LINE,0,15,126,false,false});
            while(active[1-bank]||!queues[1-bank].empty())step();
            require(active[bank]&&bpending&&read_bursts==before_reads,"other-bank hit under failed write-back");
            hold_b=false;drain();require(read_bursts==before_reads,"failed eviction must not refill");
            require(word(mem,bank*LINE)!=word(ref,bank*LINE),"failed writeback leaves stale backing victim");
            access(1-bank,bank*LINE);require(read_bursts==before_reads,"failed eviction retained dirty victim hit");
            access(bank,(2*SETS+bank)*LINE);flush();
            bins["bank"+std::to_string(bank)+"_request_WB_failure_"+(badid==2?"live_BID":(badid?"BID":"BRESP"))]++;
        }
        access(0,0,true,0xdeadbeef);next_write_error=2;flush(true);access(1,0);flush();
        access(1,LINE,true,0x789abcde);inject_bad_bid=true;flush(true);access(0,LINE);flush();bins["bad_BID_retention"]++;
        access(0,1,false,0,15,true);flush();
        // Maintenance starts with live misses and must drain their responses first.
        enqueue(0,{4096,0xaaaaaaaa,15,101,true,false});enqueue(1,{4096+LINE,0xbbbbbbbb,15,102,true,false});
        while(!active[0]||!active[1])step();flush();bins["flush_drain_inflight"]++;
        access(0,0,true,0x11112222);access(1,LINE,true,0x33334444);
        reset("destructive_dirty_reset");access(0,0);access(1,LINE);flush();
        hold_bank0=true;enqueue(0,{0,0,15,103,false,false});enqueue(1,{LINE,0,15,104,false,false});
        while(!reads[0].active||!reads[1].active)step();reset("inflight_two_read_reset");
        access(0,0);access(1,LINE);flush();
        access(0,0,true,0x55667788);access(0,SETS*LINE,true,0x99aabbcc);
        order_mode=2;enqueue(0,{2*SETS*LINE,0,15,105,false,false});
        while(!aw)step();reset("inflight_write_reset");access(0,0);flush();
        access(0,0,true,0x12344321);access(0,SETS*LINE,true,0x56788765);
        order_mode=2;enqueue(0,{2*SETS*LINE,0,15,109,false,false});
        while(!aw||wcount<1)step();reset("partial_write_reset");access(0,0);flush();
        access(0,0,true,0xaabbccdd);access(0,SETS*LINE,true,0xeeff0011);
        order_mode=1;enqueue(0,{2*SETS*LINE,0,15,110,false,false});
        while(wqueue.size()<WORDS)step();reset("orphan_W_before_AW_reset");access(0,0);flush();
        hold_bank0=true;enqueue(0,{0,0,15,106,false,false});
        while(!reads[0].active)step();allow_protocol_fault=true;inject_invalid_rid=true;
        while(!d.protocol_error)step();for(unsigned i=0;i<8;i++)step();
        reset("protocol_fault_reset_recovery");access(0,0);access(1,LINE);flush();
    }
    void random(unsigned seed,unsigned operations){
        std::mt19937 traffic(seed);
        for(unsigned i=0;i<operations;i++){
            unsigned p=traffic()%2;uint32_t a;
            switch(traffic()%3){case 0:a=(traffic()%32)*4;break;case 1:a=(traffic()%(MEM/4))*4;break;
                default:a=(traffic()%16)*SETS*LINE+(traffic()%(2*WORDS))*4;break;}
            enqueue(p,{a,uint32_t(traffic()),unsigned(traffic()%16),unsigned(i&255),bool(traffic()%2),false});
            while(queues[p].size()>2)step();if(i%137==0){drain();flush();}
        }drain();flush();
    }
    void finish(const std::string& path){
        require(accepted==completed+cancelled,"conservation");require(!reads[0].active&&!reads[1].active&&!bpending&&!aw,"unfinished AXI");
        require(rbeats+aborted_rbeats==read_bursts*WORDS&&wbeats+aborted_wbeats==write_bursts*WORDS+orphan_wbeats,"AXI beat conservation");
        std::ofstream out(path);require(bool(out),"summary open");
        out<<"{\"status\":\"PASS\",\"accepted\":"<<accepted<<",\"completed\":"<<completed<<",\"cycles\":"<<cycle
           <<",\"read_bursts\":"<<read_bursts<<",\"write_bursts\":"<<write_bursts<<",\"read_beats\":"<<rbeats
           <<",\"write_beats\":"<<wbeats<<",\"max_outstanding_reads\":"<<max_reads
           <<",\"cancelled_by_reset\":"<<cancelled<<",\"aborted_read_beats\":"<<aborted_rbeats
           <<",\"aborted_write_beats\":"<<aborted_wbeats<<",\"orphan_write_beats\":"<<orphan_wbeats
           <<",\"invalid_read_beats\":"<<invalid_rbeats<<",\"coverage\":{";
        bool first=true;for(auto bin:bins){if(!first)out<<",";first=false;out<<"\""<<bin.first<<"\":"<<bin.second;}out<<"}}\n";
        out.close();require(bool(out),"summary write");
        std::cout<<"CACHE NB PASS accepted="<<accepted<<" completed="<<completed<<" cycles="<<cycle<<" max_reads="<<max_reads<<"\n";
    }
};
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);if(argc!=5)return 2;
    try{unsigned seed=std::stoul(argv[1]);Bench b(seed);if(std::stoi(argv[3]))b.directed();
        b.random(seed,std::stoul(argv[2]));b.finish(argv[4]);return 0;
    }catch(const std::exception& e){std::cerr<<"CACHE NB FAIL "<<e.what()<<"\n";return 1;}
}
