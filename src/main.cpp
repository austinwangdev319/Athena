// Athena UCI front end.
#include "chess.hpp"
#include "eval.hpp"
#include "search.hpp"
#include "book.hpp"
#include <iostream>
#include <random>

using namespace ag;

static const char* NAME = "Athena 0.2";

struct Engine {
    SearchShared sh; Position pos; int hashMb=64, threadCount=1; bool ownBook=true; int bookDepth=16;
    int gambitRisk=65; std::thread searcher; std::mutex m; bool searching=false;
    std::vector<std::unique_ptr<Worker>> workers; std::mt19937_64 rng{std::random_device{}()};
    std::vector<BookEntry> book; int timeUse=80;
    Engine() { sh.tt.resize(hashMb); book=builtinBook(); }
    ~Engine() { stopSearch(); }
    void stopSearch() { sh.stop=true; sh.pondering=false; if(searcher.joinable()) searcher.join(); searching=false; }
    void loadLearnedStyle() {
        std::ifstream f("../athena_style.txt");
        if(!f) { f.clear(); f.open("athena_style.txt"); }
        std::string key; int value;
        while(f >> key >> value) {
            if(key=="aggression") params.aggression=std::clamp(value,100,190);
            else if(key=="gambit_risk") gambitRisk=std::clamp(value,40,130);
        }
    }
    void setPosition(std::istringstream& in) {
        std::string tok, fen; in>>tok; Position p;
        if(tok=="startpos") { p.setFen(Position::startFen()); in>>tok; }
        else if(tok=="fen") { fen.clear();
            while(in>>tok && tok!="moves") fen+=tok+" ";
            if(!p.setFen(fen)) { std::cout<<"info string invalid fen, keeping previous position"<<std::endl; return; } }
        if(tok=="moves") { std::string mv; while(in>>mv) { Move m=p.parseMove(mv); if(!m) { std::cout<<"info string illegal move "<<mv<<std::endl; break; }
            Undo u; p.make(m,u); } }
        pos=p;
    }
    void go(std::istringstream& in) {
        stopSearch();
        Limits L; std::string tok;
        while(in>>tok) {
            if(tok=="wtime") in>>L.time[0]; else if(tok=="btime") in>>L.time[1];
            else if(tok=="winc") in>>L.inc[0]; else if(tok=="binc") in>>L.inc[1];
            else if(tok=="movestogo") in>>L.movestogo; else if(tok=="depth") in>>L.depth;
            else if(tok=="nodes") in>>L.nodes; else if(tok=="movetime") in>>L.movetime;
            else if(tok=="infinite") L.infinite=true; else if(tok=="ponder") L.ponder=true;
            else if(tok=="searchmoves") { std::string mv; auto pos2=in.tellg(); while(in>>mv) { Move m=pos.parseMove(mv); if(!m) { in.clear(); in.seekg(pos2); break; } L.searchmoves.push_back(m); pos2=in.tellg(); } }
        }
        sh.limits=L; sh.stop=false; sh.pondering=L.ponder; sh.totalNodes=0; sh.threads=threadCount;
        sh.start=nowMs();
        int us=idx(pos.side); int64_t t=L.time[us], inc=L.inc[us]; int oh=sh.moveOverhead;
        sh.softMs=0; sh.hardMs=0;
        if(L.movetime>0) { sh.softMs=sh.hardMs=std::max<int64_t>(1,L.movetime-oh); }
        else if(t>0 || inc>0) {
            int mtg=L.movestogo>0?std::min(L.movestogo,50):std::max(22,40-pos.fullmove/3);
            int64_t avail=std::max<int64_t>(1,t-oh*2);
            int64_t base=avail/mtg + inc*3/4;
            // Practical targets: bullet <1s, blitz ~2-3s, longer games ~5s.
            // Remaining clock time always caps these targets.
            int64_t softCap = t<=90000 ? 650 : (t<=360000 ? 2500 : 5000);
            int64_t hardCap = t<=90000 ? 900 : (t<=360000 ? 3000 : 6500);
            sh.softMs=std::min({base*timeUse/100,avail*18/100,softCap});
            sh.hardMs=std::min({base*150*timeUse/10000,avail*25/100,hardCap});
            sh.hardMs=std::max<int64_t>(sh.hardMs,5);
            sh.softMs=std::max<int64_t>(sh.softMs,2);
            if(sh.softMs>sh.hardMs) sh.softMs=sh.hardMs;
        }
        searching=true; sh.tt.newSearch();
        searcher=std::thread([this,L]{ runSearch(L); });
    }
    void runSearch(Limits L) {
        // Opening book (never while pondering, analysing, or with restricted moves).
        if(ownBook && !L.infinite && L.searchmoves.empty() && !L.ponder && !L.depth && !L.nodes) {
            auto bm=bookMove(pos,book,bookDepth,gambitRisk,rng);
            if(bm) { std::cout<<"info string book move"<<std::endl; std::cout<<"bestmove "<<bm.uci()<<std::endl; searching=false; return; }
        }
        workers.clear();
        for(int i=0;i<threadCount;i++) { workers.emplace_back(new Worker(sh,i)); workers[i]->pos=pos; }
        std::vector<std::thread> helpers;
        for(int i=1;i<threadCount;i++) helpers.emplace_back([this,i]{ workers[i]->iterate(MAX_PLY-4); });
        workers[0]->iterate(L.depth?L.depth:MAX_PLY-4);
        // In infinite/ponder mode, UCI requires waiting for stop/ponderhit before replying.
        while(!sh.stop && (sh.pondering||L.infinite)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sh.stop=true; for(auto& h:helpers) h.join();
        Worker& w=*workers[0]; Move best=w.rootBest; 
        // pick the deepest completed helper only if it found a strictly deeper result
        for(auto& wk:workers) if(wk->rootDepth>w.rootDepth+1 && wk->rootBest) best=wk->rootBest;
        std::vector<Move> pv=w.rootPV; std::string out="bestmove "+(best?best.uci():std::string("0000"));
        if(best && pv.size()>1 && pv[0]==best) out+=" ponder "+pv[1].uci();
        std::cout<<out<<std::endl; searching=false;
    }
    void ponderhit() {
        // Ponder hit: the clock starts now.
        sh.start=nowMs(); sh.pondering=false;
    }
    void setOption(std::istringstream& in) {
        std::string tok, name, val; in>>tok; while(in>>tok && tok!="value") name+=(name.empty()?"":" ")+tok; 
        std::getline(in,val); while(!val.empty()&&val[0]==' ') val.erase(0,1);
        stopSearch();
        auto toBool=[&]{ return val=="true"; };
        if(name=="Hash") { hashMb=std::clamp(std::atoi(val.c_str()),1,1<<20); sh.tt.resize(hashMb); }
        else if(name=="Threads") { threadCount=std::clamp(std::atoi(val.c_str()),1,512); }
        else if(name=="Clear Hash") sh.tt.clear();
        else if(name=="Move Overhead") sh.moveOverhead=std::clamp(std::atoi(val.c_str()),0,5000);
        else if(name=="OwnBook") ownBook=toBool();
        else if(name=="BookFile") {
            book=builtinBook(); int skipped=0; int n=val.empty()?0:loadBookFile(val,book,&skipped);
            if(n<0) std::cout<<"info string BookFile not found: "<<val<<" (using built-in repertoire only)"<<std::endl;
            else if(n>0||skipped) std::cout<<"info string BookFile loaded "<<n<<" lines, skipped "<<skipped<<" invalid; total "<<book.size()<<std::endl;
        }
        else if(name=="TimeUse") timeUse=std::clamp(std::atoi(val.c_str()),30,300);
        else if(name=="BookDepth") bookDepth=std::clamp(std::atoi(val.c_str()),0,40);
        else if(name=="GambitRisk") gambitRisk=std::clamp(std::atoi(val.c_str()),0,150);
        else if(name=="Aggression") params.aggression=std::clamp(std::atoi(val.c_str()),50,200);
        else if(name=="Contempt") params.contempt=std::clamp(std::atoi(val.c_str()),-100,100);
    }
};

int main(int argc,char** argv) {
    std::ios::sync_with_stdio(false); std::cin.tie(nullptr);
    Engine e; std::string line;
    if(argc>1 && std::string(argv[1])=="bench") {
        e.sh.showInfo=false; uint64_t total=0; int64_t t0=nowMs();
        const char* fens[]={Position::startFen().c_str(),"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1","r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"};
        for(auto f:fens){ e.pos.setFen(f); e.sh.stop=false; e.sh.limits=Limits(); e.sh.limits.depth=12; e.sh.start=nowMs(); e.sh.totalNodes=0;
            Worker w(e.sh,0); w.pos=e.pos; w.iterate(12); total+=w.nodes; }
        int64_t t=std::max<int64_t>(1,nowMs()-t0); std::cout<<total<<" nodes "<<total*1000/t<<" nps"<<std::endl; return 0; }
    while(std::getline(std::cin,line)) {
        std::istringstream in(line); std::string cmd; in>>cmd;
        if(cmd=="uci") {
            std::cout<<"id name "<<NAME<<"\nid author Austin Wang\n"
                "option name Hash type spin default 64 min 1 max 1048576\n"
                "option name Threads type spin default 1 min 1 max 512\n"
                "option name Clear Hash type button\n"
                "option name Ponder type check default false\n"
                "option name Move Overhead type spin default 60 min 0 max 5000\n"
                "option name OwnBook type check default true\n"
                "option name BookFile type string default <empty>\n"
                "option name TimeUse type spin default 80 min 30 max 300\n"
                "option name BookDepth type spin default 16 min 0 max 40\n"
                "option name GambitRisk type spin default 65 min 0 max 150\n"
                "option name Aggression type spin default 110 min 50 max 200\n"
                "option name Contempt type spin default 25 min -100 max 100\n"
                "uciok"<<std::endl;
        } else if(cmd=="isready") std::cout<<"readyok"<<std::endl;
        else if(cmd=="ucinewgame") { e.stopSearch(); e.sh.tt.clear(); e.pos=Position(); e.loadLearnedStyle(); }
        else if(cmd=="position") e.setPosition(in);
        else if(cmd=="go") e.go(in);
        else if(cmd=="stop") { e.sh.pondering=false; e.sh.stop=true; if(e.searcher.joinable()) e.searcher.join(); }
        else if(cmd=="ponderhit") e.ponderhit();
        else if(cmd=="setoption") e.setOption(in);
        else if(cmd=="d") std::cout<<e.pos.fen()<<std::endl;
        else if(cmd=="perft") { int d=1; in>>d; std::cout<<perft(e.pos,d)<<std::endl; }
        else if(cmd=="eval") std::cout<<"eval (stm) "<<evaluate(e.pos)<<std::endl;
        else if(cmd=="quit") break;
    }
    e.stopSearch(); return 0;
}
