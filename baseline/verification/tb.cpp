#include "Vcache.h"
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
constexpr unsigned SETS=TEST_SETS, WORDS=TEST_WORDS, LINE=4*WORDS, STRIDE=SETS*LINE;
constexpr unsigned MEM=65536;
struct Request {
    uint32_t addr=0, data=0;
    unsigned mask=0, id=0;
    bool write=false, error=false;
};
struct Pending {
    Request req;
    uint32_t expected=0;
    std::array<uint8_t,4> old{};
    uint64_t accepted=0;
};
class Bench {
public:
    Vcache dut;
    std::mt19937 bus_rng;
    std::vector<uint8_t> architectural, backing;
    std::deque<Request> queues[2];
    bool active[2]={false,false};
    Pending pending[2];
    uint64_t cycles=0, accepted=0, completed=0, cancelled=0, reads=0, writes=0;
    uint64_t read_beats=0, write_beats=0, hits=0, misses=0, latency=0;
    uint64_t aborted_read_beats=0;
    uint64_t aborted_write_beats=0, orphan_write_beats=0;
    unsigned expected_turn=0;
    std::map<std::string,uint64_t> coverage;
    std::vector<uint32_t> evictions;
    int order_mode=0, next_b_error=0, next_r_error=-1, next_bad_rid=-1, next_bad_last=-1;
    bool bad_bid=false, next_bad_bid=false;
    unsigned response_hold=0, maintenance_hold=0;
    bool aw_active=false, read_active=false, b_pending=false, b_hold=false, r_hold=false;
    uint32_t write_addr=0, read_addr=0;
    unsigned written=0, read_index=0, aw_age=0, w_age=0;
    int b_error=0, r_error=-1, bad_rid=-1, bad_last=-1;
    std::deque<uint32_t> write_queue;
    bool saw_wlast=false, aw_stalled=false, w_stalled=false, ar_stalled=false;
    uint32_t last_aw=0,last_w=0,last_ar=0,last_rsp=0;
    unsigned last_rsp_valid=0,last_id=0,last_error=0,last_last=0;
    bool rsp_stalled=false, flush_stalled=false;
    unsigned last_flush_error=0;

    explicit Bench(unsigned seed):bus_rng(seed^0x9e3779b9),architectural(MEM),backing(MEM) {
        for(unsigned i=0;i<MEM;i++) architectural[i]=backing[i]=uint8_t((i*37u)^(i>>5));
        reset();
    }
    void need(bool ok,const std::string& witness) {
        if(!ok) throw std::runtime_error(witness+" at cycle "+std::to_string(cycles));
    }
    uint32_t word(const std::vector<uint8_t>& memory,uint32_t a) {
        need(a+3<MEM,"address range");
        uint32_t v=0;
        for(unsigned i=0;i<4;i++) v|=uint32_t(memory[a+i])<<(i*8);
        return v;
    }
    void reset() {
        dut.rst_n=0; dut.req_valid=0; dut.flush_valid=0; dut.flush_done_ready=0;
        dut.awready=0; dut.wready=0; dut.bvalid=0; dut.arready=0; dut.rvalid=0;
        dut.rsp_ready=0;
        for(unsigned i=0;i<3;i++){dut.clk=0;dut.eval();dut.clk=1;dut.eval();}
        dut.rst_n=1;
        if(read_active)aborted_read_beats+=WORDS-read_index;
        int64_t balance=int64_t(writes*WORDS+orphan_write_beats)-int64_t(write_beats+aborted_write_beats);
        if(balance>0)aborted_write_beats+=uint64_t(balance);
        if(balance<0)orphan_write_beats+=uint64_t(-balance);
        aw_active=read_active=b_pending=b_hold=r_hold=saw_wlast=false;
        write_queue.clear(); written=read_index=aw_age=w_age=0;
        aw_stalled=w_stalled=ar_stalled=rsp_stalled=flush_stalled=false;
        expected_turn=0;
        for(unsigned p=0;p<2;p++){if(active[p])cancelled++;active[p]=false;queues[p].clear();}
        architectural=backing;
        coverage["coordinated_reset"]++;
    }
    void enqueue(unsigned port,Request req) {queues[port].push_back(req);}
    void controls() {
        dut.req_valid=0; dut.req_addr=0; dut.req_data=0; dut.req_id=0;
        dut.req_write=0;dut.req_mask=0;
        for(unsigned p=0;p<2;p++) if(!active[p]&&!queues[p].empty()) {
            const auto& r=queues[p].front();
            dut.req_valid|=1u<<p;
            dut.req_addr|=uint64_t(r.addr)<<(32*p);
            dut.req_data|=uint64_t(r.data)<<(32*p);
            dut.req_id|=r.id<<(8*p);
            dut.req_write|=unsigned(r.write)<<p;
            dut.req_mask|=r.mask<<(4*p);
        }
        dut.rsp_ready=response_hold?0:(bus_rng()%4);
        if(response_hold) --response_hold;
        dut.flush_done_ready=maintenance_hold?0:1;
        if(maintenance_hold) --maintenance_hold;
        dut.awready=bus_rng()%4!=0;
        dut.wready=bus_rng()%3!=0;
        if(order_mode==1){dut.awready=aw_age>WORDS+8;dut.wready=1;}
        if(order_mode==2){dut.awready=1;dut.wready=w_age>WORDS+8;}
        dut.arready=bus_rng()%3!=0;
        if(b_pending && !b_hold && bus_rng()%4==0)b_hold=true;
        dut.bvalid=b_hold; dut.bresp=b_error; dut.bid=bad_bid;
        if(read_active && !r_hold && bus_rng()%3!=0)r_hold=true;
        dut.rvalid=r_hold;
        dut.rdata=read_active?word(backing,read_addr+4*read_index):0;
        dut.rresp=int(read_index)==r_error?2:0;
        dut.rid=int(read_index)==bad_rid?1:0;
        dut.rlast=(read_index==WORDS-1)^(int(read_index)==bad_last);
    }
    void stability() {
        need(!aw_stalled||(dut.awvalid&&dut.awaddr==last_aw),"AW stalled payload");
        need(!w_stalled||(dut.wvalid&&dut.wdata==last_w&&dut.wlast==last_last),"W stalled payload");
        need(!ar_stalled||(dut.arvalid&&dut.araddr==last_ar),"AR stalled payload");
        need(!rsp_stalled||(dut.rsp_valid==last_rsp_valid&&dut.rsp_data==last_rsp&&
             dut.rsp_id==last_id&&dut.rsp_error==last_error),"response stalled payload");
        need(!flush_stalled||(dut.flush_done&&dut.flush_error==last_flush_error),"flush stalled payload");
        aw_stalled=dut.awvalid&&!dut.awready;
        w_stalled=dut.wvalid&&!dut.wready;
        ar_stalled=dut.arvalid&&!dut.arready;
        rsp_stalled=dut.rsp_valid&&(dut.rsp_valid&dut.rsp_ready)==0;
        flush_stalled=dut.flush_done&&!dut.flush_done_ready;
        last_aw=dut.awaddr;last_w=dut.wdata;last_last=dut.wlast;last_ar=dut.araddr;
        last_rsp=dut.rsp_data;last_rsp_valid=dut.rsp_valid;last_id=dut.rsp_id;
        last_error=dut.rsp_error;last_flush_error=dut.flush_error;
        if(aw_stalled)coverage["AW_stall"]++;
        if(w_stalled)coverage["W_stall"]++;
        if(ar_stalled)coverage["AR_stall"]++;
        if(rsp_stalled)coverage["response_stall"]++;
        if(flush_stalled)coverage["flush_response_stall"]++;
        if(read_active&&!dut.rvalid)coverage["R_gap"]++;
        if(b_pending&&!dut.bvalid)coverage["B_gap"]++;
    }
    void profile(bool write,uint32_t address) {
        need((address%LINE)==0 && (address%4096)+LINE<=4096,"AXI alignment/boundary");
        need((write?dut.awlen:dut.arlen)==WORDS-1,"AXI burst length");
        need((write?dut.awsize:dut.arsize)==2,"AXI beat size");
        need((write?dut.awburst:dut.arburst)==1,"AXI INCR");
        need((write?dut.awid:dut.arid)==0,"AXI ID");
        need((write?dut.awlock:dut.arlock)==0,"AXI lock");
        need((write?dut.awcache:dut.arcache)==0,"AXI cache attribute");
        need((write?dut.awprot:dut.arprot)==0,"AXI protection attribute");
        need((write?dut.awqos:dut.arqos)==0,"AXI QoS");
    }
    void step() {
        if(cycles>=200000)throw std::runtime_error("cycle timeout");
        dut.clk=0; controls();dut.eval();stability();
        if(dut.req_valid==3)coverage["two_requesters_contend"]++;
        for(unsigned p=0;p<2;p++) if((dut.req_valid&dut.req_ready)&(1u<<p)) {
            need(!active[p],"duplicate admission");
            if(dut.req_valid==3) {
                need(p==expected_turn,"round-robin contested admission");
                coverage["round_robin_contested_grant"]++;
            }
            expected_turn=1-p;
            auto r=queues[p].front();queues[p].pop_front();active[p]=true;
            pending[p].req=r;pending[p].expected=r.write?0:word(architectural,r.addr&~3u);
            pending[p].accepted=cycles;
            for(unsigned i=0;i<4;i++) pending[p].old[i]=architectural[r.addr+i];
            if(r.write&&r.addr%4==0)for(unsigned i=0;i<4;i++)if(r.mask&(1u<<i))
                architectural[r.addr+i]=uint8_t(r.data>>(8*i));
            accepted++;coverage["requester"+std::to_string(p)]++;
            if(r.write)coverage["mask"+std::to_string(r.mask)]++;
        }
        for(unsigned p=0;p<2;p++) if((dut.rsp_valid&dut.rsp_ready)&(1u<<p)) {
            need(active[p],"unsolicited/duplicate response");
            auto& q=pending[p];
            need(dut.rsp_id==q.req.id,"response identity");
            need(bool(dut.rsp_error)==q.req.error,"response status");
            if(!q.req.error)need(dut.rsp_data==q.expected,"architectural read/store response");
            else {
                if(q.req.write)for(unsigned i=0;i<4;i++)architectural[q.req.addr+i]=q.old[i];
                coverage["request_error"]++;
            }
            active[p]=false;completed++;latency+=cycles-q.accepted+1;
        }
        if(dut.awvalid)aw_age++;
        if(dut.wvalid)w_age++;
        if(dut.awvalid&&dut.awready) {
            profile(true,dut.awaddr);need(!aw_active&&!b_pending,"multiple AW");
            aw_active=true;write_addr=dut.awaddr;written=0;
            evictions.push_back(write_addr);writes++;
            b_error=next_b_error;next_b_error=0;
            bad_bid=next_bad_bid;next_bad_bid=false;
            coverage[write_queue.empty()?"AW_before_W":"W_before_AW"]++;
        }
        if(dut.wvalid&&dut.wready) {
            need(!saw_wlast,"W after WLAST");
            need(dut.wstrb==15,"writeback strobes");
            write_queue.push_back(dut.wdata);saw_wlast=dut.wlast;write_beats++;
        }
        while(aw_active&&!write_queue.empty()) {
            need(written<WORDS,"excess W beats");
            uint32_t value=write_queue.front();write_queue.pop_front();
            need(value==word(architectural,write_addr+4*written),"dirty writeback byte integrity");
            for(unsigned i=0;i<4;i++)backing[write_addr+4*written+i]=uint8_t(value>>(8*i));
            written++;
        }
        if(aw_active&&saw_wlast&&write_queue.empty()) {
            need(written==WORDS,"WLAST/beat count");
            aw_active=false;b_pending=true;aw_age=w_age=0;
        }
        if(dut.bvalid&&dut.bready) {
            need(b_pending&&saw_wlast&&written==WORDS,"B before AW/W completion");
            b_pending=b_hold=saw_wlast=false;written=0;
            if(b_error||bad_bid)coverage["writeback_error"]++;
        }
        if(dut.arvalid&&dut.arready) {
            profile(false,dut.araddr);need(!read_active&&!b_pending&&!aw_active,"read/write ordering");
            read_active=true;read_addr=dut.araddr;read_index=0;reads++;
            r_error=next_r_error;next_r_error=-1;
            bad_rid=next_bad_rid;next_bad_rid=-1;
            bad_last=next_bad_last;next_bad_last=-1;
        }
        if(dut.rvalid&&dut.rready) {
            need(read_active,"R without AR");read_beats++;
            if(dut.rresp)coverage["refill_error_beat"+std::to_string(read_index)]++;
            if(dut.rid)coverage["bad_RID"]++;
            if(int(read_index)==bad_last)coverage["bad_RLAST"]++;
            read_index++;r_hold=false;
            if(read_index==WORDS)read_active=false;
        }
        bool hit=dut.event_hit,miss=dut.event_miss;
        dut.clk=1;dut.eval();cycles++;hits+=hit;misses+=miss;
    }
    void drain() {
        while(!queues[0].empty()||!queues[1].empty()||active[0]||active[1])step();
        step();
    }
    void access(unsigned p,uint32_t addr,bool write=false,uint32_t value=0,unsigned mask=15,bool error=false) {
        enqueue(p,{addr,value,mask,unsigned((accepted*17+31)&255),write,error});drain();
    }
    void flush(bool error=false) {
        drain();dut.flush_valid=1;
        while(!dut.flush_ready)step();
        step();dut.flush_valid=0;
        while(!dut.flush_done)step();
        need(bool(dut.flush_error)==error,"flush status");
        maintenance_hold=7;
        while(dut.flush_done)step();
        if(!error)need(architectural==backing,"final flushed backing bytes");
        coverage[error?"flush_error":"flush_success"]++;
    }
    void directed() {
        access(0,0);auto old_reads=reads;access(1,0);need(reads==old_reads,"warm hit traffic");
        coverage["cold_and_hit"]++;
        for(unsigned mask=0;mask<16;mask++){
            access(mask%2,4,true,0x715ac39eu^(mask*0x01010101u),mask);access(1-mask%2,4);
        }
        access(0,STRIDE,true,0xa1b2c3d4);access(1,0);
        auto evicted=evictions.size();access(0,2*STRIDE);
        need(evictions.size()==evicted+1 && evictions.back()==STRIDE,"LRU dirty victim selection");
        coverage["both_ways_LRU_dirty_eviction"]++;
        flush();
        for(unsigned mask=1;mask<16;mask++) {
            access(mask%2,512,true,0xf00dcafeu^mask,mask);access(1-mask%2,512);flush();
            coverage["allocate_mask"+std::to_string(mask)]++;
        }
        for(int mode=1;mode<=2;mode++) {
            order_mode=mode;
            access(0,0,true,0x12345678);access(1,STRIDE,true,0xabcdef99);access(0,2*STRIDE);
            flush();
        }
        order_mode=0;
        enqueue(0,{128,0,15,1,false,false});enqueue(1,{132,0,15,2,false,false});drain();
        enqueue(0,{132,0,15,3,false,false});enqueue(1,{128,0,15,4,false,false});drain();
        coverage["simultaneous_request_routing"]++;
        response_hold=30;access(0,128);
        auto bus_before=reads+writes;access(1,129,false,0,15,true);
        need(reads+writes==bus_before,"unaligned has no memory effect");
        coverage["unaligned_rejection"]++;
        flush();
        for(unsigned beat=0;beat<WORDS;beat++) {
            next_r_error=int(beat);access(0,1024+beat*LINE,false,0,15,true);
            access(1,1024+beat*LINE);
        }
        flush();
        next_bad_rid=0;access(0,2048,false,0,15,true);access(1,2048);
        next_bad_last=0;access(0,2048+LINE,false,0,15,true);access(1,2048+LINE);
        next_bad_last=WORDS-1;access(0,2048+2*LINE,false,0,15,true);access(1,2048+2*LINE);
        flush();
        for(int status=1;status<=3;status++) {
            access(0,0,true,0xaabbee00u+status);access(1,STRIDE,true,0xcccc0100u+status);
            next_b_error=status;access(0,2*STRIDE,false,0,15,true);
            auto r=reads;access(1,0);need(reads==r,"failed eviction retained dirty victim");
            coverage["dirty_error_retention"]++;
            access(0,2*STRIDE);flush();
        }
        access(0,0,true,0x44332211);next_bad_bid=true;flush(true);access(1,0);flush();
        coverage["bad_BID"]++;
        access(0,0,true,0x55555555);next_b_error=2;flush(true);flush();
        access(0,0,true,0xffffffff);reset();access(1,0);
        coverage["destructive_dirty_reset"]++;
        // Abort a refill only together with the memory model, as contracted.
        enqueue(0,{4096,0,15,99,false,false});
        while(!read_active)step();reset();access(1,4096);
        coverage["inflight_refill_reset"]++;
        access(0,0,true,0x17171717);access(1,STRIDE,true,0x18181818);
        enqueue(0,{2*STRIDE,0,15,102,false,false});
        while(!aw_active&&!b_pending)step();reset();access(1,0);flush();
        coverage["inflight_write_reset"]++;
        // Boundary addresses use actual full line bursts.
        access(0,4096-LINE);access(1,4096);flush();
        coverage["4KiB_adjacent_lines"]++;
    }
    void random(unsigned seed,unsigned count) {
        std::mt19937 traffic(seed);
        for(unsigned i=0;i<count;i++) {
            unsigned p=traffic()%2;
            uint32_t a;
            switch(traffic()%3) {
                case 0:a=(traffic()%(2*SETS*WORDS))*4;break;
                case 1:a=(traffic()%(MEM/4))*4;break;
                default:a=(traffic()%16)*STRIDE+(traffic()%WORDS)*4;break;
            }
            enqueue(p,{a,uint32_t(traffic()),unsigned(traffic()%16),unsigned(i&255),bool(traffic()%2),false});
            while(queues[p].size()>2)step();
            if(i%137==0){drain();flush();}
        }
        drain();flush();
    }
    void summary(const std::string& path) {
        need(accepted==completed+cancelled,"response conservation");
        need(!aw_active&&!read_active&&!b_pending,"outstanding AXI at finish");
        std::ofstream out(path);need(bool(out),"summary open");
        out<<"{\"status\":\"PASS\",\"cycles\":"<<cycles<<",\"accepted\":"<<accepted
           <<",\"completed\":"<<completed<<",\"cancelled_by_reset\":"<<cancelled
           <<",\"read_bursts\":"<<reads<<",\"write_bursts\":"<<writes
           <<",\"read_beats\":"<<read_beats<<",\"write_beats\":"<<write_beats
           <<",\"aborted_read_beats\":"<<aborted_read_beats
           <<",\"aborted_write_beats\":"<<aborted_write_beats
           <<",\"orphan_write_beats\":"<<orphan_write_beats
           <<",\"hit_events\":"<<hits<<",\"miss_events\":"<<misses<<",\"latency_sum\":"<<latency
           <<",\"coverage\":{";
        bool first=true;for(const auto& bin:coverage){if(!first)out<<",";first=false;
            out<<"\""<<bin.first<<"\":"<<bin.second;}
        out<<"}}\n";out.close();need(bool(out),"summary write");
        std::cout<<"CACHE PASS accepted="<<accepted<<" completed="<<completed<<" cycles="<<cycles<<"\n";
    }
};
int main(int argc,char** argv) {
    Verilated::commandArgs(argc,argv);
    if(argc!=5){std::cerr<<"usage: Vcache seed operations directed summary.json\n";return 2;}
    try {
        unsigned seed=std::stoul(argv[1]),operations=std::stoul(argv[2]);
        Bench b(seed);
        if(std::stoi(argv[3]))b.directed();
        if(operations)b.random(seed,operations);
        b.summary(argv[4]);
        return 0;
    } catch(const std::exception& e){std::cerr<<"CACHE FAIL "<<e.what()<<"\n";return 1;}
}
