// Athena search: iterative-deepening PVS with Lazy SMP over a shared lock-less hash table.
#pragma once
#include "chess.hpp"
#include "eval.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

namespace ag {
using Clock = std::chrono::steady_clock;
inline int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

struct Limits {
    int depth=0; uint64_t nodes=0; int64_t movetime=0, time[2]={0,0}, inc[2]={0,0}; int movestogo=0;
    bool infinite=false, ponder=false; std::vector<Move> searchmoves;
};

// ---------------- Transposition table (16 bytes/entry, XOR-verified, tolerant of races) -------------
enum Bound { B_NONE=0, B_UPPER=1, B_LOWER=2, B_EXACT=3 };
struct TTData { Move move; int score=0, depth=-1; int bound=B_NONE; bool hit=false; };
class TT {
    struct Entry { std::atomic<uint64_t> key{0}, data{0}; };
    Entry* table=nullptr; size_t mask=0, count=0; std::atomic<uint8_t> age{0};
public:
    ~TT() { std::free(table); }
    void resize(size_t mb) {
        std::free(table); size_t n=1; while((n*2)*sizeof(Entry) <= mb*1024*1024) n*=2;
        count=n; mask=n-1; table=static_cast<Entry*>(std::calloc(n,sizeof(Entry)));
        if(!table) { count=1<<16; mask=count-1; table=static_cast<Entry*>(std::calloc(count,sizeof(Entry))); }
    }
    void clear() { std::memset(static_cast<void*>(table),0,count*sizeof(Entry)); age=0; }
    void newSearch() { age=(age+1)&63; }
    static int toTT(int s,int ply) { return s>=MATE-MAX_PLY ? s+ply : (s<=-MATE+MAX_PLY ? s-ply : s); }
    static int fromTT(int s,int ply) { return s>=MATE-MAX_PLY ? s-ply : (s<=-MATE+MAX_PLY ? s+ply : s); }
    void store(Key k,Move m,int score,int depth,int bound,int ply) {
        Entry& e=table[k&mask]; uint64_t old=e.data.load(std::memory_order_relaxed);
        uint64_t oldKey=e.key.load(std::memory_order_relaxed)^old;
        if(oldKey==k && !m) m=Move(uint32_t(old&0x1FFFFFF));
        // replacement: always replace on same key, older age, or deeper/equal
        int oldDepth=int((old>>49)&0xFF)-1; int oldAge=int((old>>43)&0x3F);
        if(oldKey!=k && old!=0 && oldAge==age && oldDepth>depth+3 && bound!=B_EXACT) return;
        // bit layout: move 0-24 | score 25-40 | bound 41-42 | age 43-48 | depth+1 49-56
        uint64_t d2=uint64_t(m.v&0x1FFFFFF)|(uint64_t(uint16_t(int16_t(toTT(score,ply))))<<25)
            |(uint64_t(bound&3)<<41)|(uint64_t(age.load())<<43)|(uint64_t(std::min(std::max(depth+1,0),255))<<49);
        e.data.store(d2,std::memory_order_relaxed); e.key.store(k^d2,std::memory_order_relaxed);
    }
    TTData probe(Key k,int ply) const {
        TTData d; const Entry& e=table[k&mask]; uint64_t dt=e.data.load(std::memory_order_relaxed);
        if(dt==0||(e.key.load(std::memory_order_relaxed)^dt)!=k) return d;
        d.hit=true; d.move=Move(uint32_t(dt&0x1FFFFFF)); d.score=fromTT(int16_t((dt>>25)&0xFFFF),ply);
        d.bound=int((dt>>41)&3); d.depth=int((dt>>49)&0xFF)-1; return d;
    }
    int hashfull() const { int n=0; size_t lim=std::min<size_t>(1000,count); for(size_t i=0;i<lim;i++) n+=table[i].data.load(std::memory_order_relaxed)!=0; return int(n*1000/lim); }
};

// ---------------- Static exchange evaluation -------------------------------------------------------
inline int leastAttacker(const std::array<int8_t,128>& b,int sq,int side) {
    int best=-1,bestV=1<<30;
    auto consider=[&](int s,int p){ int v=value[std::abs(p)]; if(v<bestV){bestV=v;best=s;} };
    for(int k:{-1,1}){int t=sq-side*16+k; if(valid(t)&&b[t]==side*PAWN)consider(t,b[t]);}
    for(int d:knightSteps){int t=sq+d; if(valid(t)&&b[t]==side*KNIGHT)consider(t,b[t]);}
    for(int d:bishopSteps)for(int t=sq+d;valid(t);t+=d)if(b[t]){if(b[t]==side*BISHOP||b[t]==side*QUEEN)consider(t,b[t]);break;}
    for(int d:rookSteps)for(int t=sq+d;valid(t);t+=d)if(b[t]){if(b[t]==side*ROOK||b[t]==side*QUEEN)consider(t,b[t]);break;}
    for(int d:kingSteps){int t=sq+d; if(valid(t)&&b[t]==side*KING)consider(t,b[t]);}
    return best;
}
inline int see(const Position& pos,Move m) {
    auto b=pos.b; int to=m.to(),side=pos.side; int gain[40],d=0;
    int victim = m.ep()?PAWN:std::abs(b[to]);
    gain[0]=value[victim]; int pt=std::abs(b[m.from()]);
    if(m.promotion()){gain[0]+=value[m.promotion()]-value[PAWN];pt=m.promotion();}
    b[m.from()]=0; if(m.ep())b[to-side*16]=0; b[to]=side*pt; side=-side;
    while(d<38){
        int from=leastAttacker(b,to,side); if(from<0)break;
        int att=std::abs(b[from]); ++d; gain[d]=value[pt]-gain[d-1];
        if(att==KING){ // king may only capture if square then not defended
            if(leastAttacker(b,to,-side)>=0){--d;break;}
        }
        b[from]=0; b[to]=side*att; pt=att; side=-side;
        if(std::max(-gain[d-1],gain[d])<0)break;
    }
    while(d>0){gain[d-1]=-std::max(-gain[d-1],gain[d]);--d;}
    return gain[0];
}

struct SearchShared {
    TT tt; std::atomic<bool> stop{false}, pondering{false}; Limits limits;
    int64_t start=0, softMs=0, hardMs=0; std::atomic<uint64_t> totalNodes{0};
    int threads=1; int moveOverhead=60; bool showInfo=true;
};

class Worker {
public:
    SearchShared& sh; Position pos; int id; uint64_t nodes=0; int selDepth=0;
    Move killers[MAX_PLY+4][2]; int history[2][128][128]; Move counter[128][128];
    Move pvTable[MAX_PLY+2][MAX_PLY+2]; int pvLen[MAX_PLY+2]; int staticEval[MAX_PLY+4]; Move prevMove[MAX_PLY+4];
    Move rootBest; int rootScore=0; int rootDepth=0; std::vector<Move> rootPV;
    int rootSide=WHITE; bool aborted=false;
    int lmr[64][64];
    Worker(SearchShared& s,int i):sh(s),id(i) {
        std::memset(history,0,sizeof(history)); std::memset(counter,0,sizeof(counter));
        for(auto& k:killers)k[0]=k[1]=Move(); 
        for(int d=1;d<64;d++)for(int m=1;m<64;m++)lmr[d][m]=int(0.6+std::log(d)*std::log(m)/2.3);
    }
    bool shouldStop() {
        if(sh.stop.load(std::memory_order_relaxed))return true;
        if(sh.limits.nodes && sh.totalNodes.load(std::memory_order_relaxed)+nodes>=sh.limits.nodes){sh.stop=true;return true;}
        if(id==0 && (nodes&1023)==0){
            if(!sh.pondering && !sh.limits.infinite && sh.hardMs>0 && nowMs()-sh.start>=sh.hardMs){sh.stop=true;return true;}
        }
        return false;
    }
    int drawScore(int ply) const { return (ply&1)?params.contempt:-params.contempt; }
    int orderScore(Move m,Move ttm,int ply,Move prev) {
        if(m==ttm)return 1<<30;
        if(m.tactical()){
            int victim=m.ep()?PAWN:std::abs(pos.b[m.to()]),att=std::abs(pos.b[m.from()]);
            int s=value[victim]*16-att+ (m.promotion()?value[m.promotion()]*8:0);
            return (see(pos,m)>=0?(1<<24):0)+s;
        }
        if(m==killers[ply][0])return (1<<23)+2;
        if(m==killers[ply][1])return (1<<23)+1;
        int s=history[idx(pos.side)][m.from()][m.to()];
        if(prev && m==counter[prev.from()][prev.to()])s+=1<<14;
        return s;
    }
    void sortMoves(std::vector<Move>& mv,std::vector<int>& sc,int i){
        int b=i; for(int j=i+1;j<(int)mv.size();j++)if(sc[j]>sc[b])b=j;
        if(b!=i){std::swap(mv[b],mv[i]);std::swap(sc[b],sc[i]);}
    }
    void updateHistory(int side,Move m,int bonus){
        int& h=history[idx(side)][m.from()][m.to()];
        h+=bonus-h*std::abs(bonus)/16384;
    }
    bool isAdvancedPassedPawn(int sq, int col) const {
        if(!valid(sq) || pos.b[sq] != col*PAWN) return false;
        int relativeRank=col==WHITE ? rankOf(sq) : 7-rankOf(sq);
        if(relativeRank<4) return false;
        const int fw=col*16;
        for(int df=-1;df<=1;df++) {
            for(int t=sq+fw+df;valid(t);t+=fw) {
                if(pos.b[t]==-col*PAWN) return false;
            }
        }
        return true;
    }
    bool hasUrgentPassedPawn() const {
        for(int sq=0;sq<128;sq++) {
            if(!valid(sq)){sq+=7;continue;}
            int p=pos.b[sq];
            if(std::abs(p)==PAWN && isAdvancedPassedPawn(sq,color(p))) {
                int relativeRank=color(p)==WHITE ? rankOf(sq) : 7-rankOf(sq);
                if(relativeRank>=5) return true;
            }
        }
        return false;
    }
    bool movesPassedPawn(Move m) const {
        int p=pos.b[m.from()];
        return std::abs(p)==PAWN && isAdvancedPassedPawn(m.from(),color(p));
    }
    int qsearch(int alpha,int beta,int ply) {
        ++nodes; selDepth=std::max(selDepth,ply); pvLen[ply]=0;
        if(shouldStop())return 0;
        if(ply>=MAX_PLY-1)return evaluate(pos);
        if(pos.halfmove>=100||pos.insufficient()) return drawScore(ply);
        bool inChk=pos.inCheck();
        TTData tt=sh.tt.probe(pos.hash,ply);
        if(tt.hit && ((tt.bound==B_EXACT)||(tt.bound==B_LOWER&&tt.score>=beta)||(tt.bound==B_UPPER&&tt.score<=alpha))) return tt.score;
        int stand=-INF;
        if(!inChk){
            stand=evaluate(pos);
            if(stand>=beta)return stand;
            if(stand>alpha)alpha=stand;
        }
        std::vector<Move> mv; pos.pseudo(mv,!inChk);
        std::vector<int> sc(mv.size());
        for(size_t i=0;i<mv.size();i++){
            sc[i]=orderScore(mv[i],tt.move,ply,Move());
        }
        int best=inChk?-MATE+ply:stand, legal=0; Move bestMove;
        for(int i=0;i<(int)mv.size();i++){
            sortMoves(mv,sc,i); Move m=mv[i];
            if(!inChk){
                int gainv=m.ep()?value[PAWN]:value[std::abs(pos.b[m.to()])];
                if(stand+gainv+220<alpha&&!m.promotion())continue;        // delta pruning; never prune promotions
                if(see(pos,m)<0&&!m.promotion())continue;
            }
            Undo u; if(!pos.make(m,u))continue; ++legal;
            int s=-qsearch(-beta,-alpha,ply+1); pos.undo(m,u);
            if(sh.stop.load(std::memory_order_relaxed))return 0;
            if(s>best){best=s;bestMove=m; if(s>alpha){alpha=s; if(s>=beta)break;}}
        }
        if(inChk&&!legal)return -MATE+ply;
        if(sh.stop.load(std::memory_order_relaxed))return 0;
        sh.tt.store(pos.hash,bestMove,best,0,best>=beta?B_LOWER:B_UPPER,ply);
        return best;
    }
    int search(int depth,int alpha,int beta,int ply,bool pv,bool allowNull,Move excluded=Move()) {
        pvLen[ply]=0;
        if(depth<=0)return qsearch(alpha,beta,ply);
        ++nodes; selDepth=std::max(selDepth,ply);
        if(shouldStop())return 0;
        bool root=ply==0;
        if(!root){
            if(pos.halfmove>=100){ if(!pos.inCheck()||!pos.legal().empty())return drawScore(ply); }
            if(pos.repetition(2)||pos.insufficient())return drawScore(ply);
            if(ply>=MAX_PLY-1)return evaluate(pos);
            alpha=std::max(alpha,-MATE+ply); beta=std::min(beta,MATE-ply-1); if(alpha>=beta)return alpha;
        }
        bool inChk=pos.inCheck();
        if(inChk)depth++;
        TTData tt=sh.tt.probe(pos.hash,ply);
        Move ttm=tt.hit?tt.move:Move();
        if(!pv&&!excluded&&tt.hit&&tt.depth>=depth&&(tt.bound==B_EXACT||(tt.bound==B_LOWER&&tt.score>=beta)||(tt.bound==B_UPPER&&tt.score<=alpha))&&pos.halfmove<90)
            return tt.score;
        int eval = inChk?-INF:evaluate(pos); staticEval[ply]=eval;
        bool improving = !inChk && ply>=2 && staticEval[ply-2]!=-INF && eval>staticEval[ply-2];
        int npm=pos.nonPawnMaterial(pos.side);
        const bool urgentPawnRace=hasUrgentPassedPawn();
        Move prev=ply>0?prevMove[ply-1]:Move();
        if(!pv&&!inChk&&!excluded){
            if(!urgentPawnRace&&depth<=6&&eval-85*depth>=beta&&std::abs(beta)<MATE-MAX_PLY&&npm>0)return eval;   // reverse futility
            if(!urgentPawnRace&&depth<=2&&eval+280*depth<alpha){int q=qsearch(alpha,beta,ply);if(q<=alpha)return q;}   // razoring
            if(!urgentPawnRace&&allowNull&&depth>=3&&eval>=beta&&npm>value[BISHOP]&&beta>-MATE+MAX_PLY){
                int R=3+depth/4+std::min((eval-beta)/200,2); Undo u; pos.makeNull(u); prevMove[ply]=Move();
                int s=-search(depth-1-R,-beta,-beta+1,ply+1,false,false); pos.undoNull(u);
                if(sh.stop.load(std::memory_order_relaxed))return 0;
                if(s>=beta){ if(s>=MATE-MAX_PLY)s=beta;
                    if(depth<10)return s;
                    int v=search(depth-R,beta-1,beta,ply,false,false); if(v>=beta)return s; }
            }
        }
        if(depth>=5&&!ttm&&pv)depth--; // cheap internal iterative reduction
        std::vector<Move> mv; pos.pseudo(mv); std::vector<int> sc(mv.size());
        for(size_t i=0;i<mv.size();i++)sc[i]=orderScore(mv[i],ttm,ply,prev);
        int best=-INF,origAlpha=alpha,legal=0,quietCount=0; Move bestMove; Move quiets[64];
        for(int i=0;i<(int)mv.size();i++){
            sortMoves(mv,sc,i); Move m=mv[i];
            if(m==excluded)continue;
            if(root&&!sh.limits.searchmoves.empty()&&std::find(sh.limits.searchmoves.begin(),sh.limits.searchmoves.end(),m)==sh.limits.searchmoves.end())continue;
            bool quiet=!m.tactical();
            const bool passerMove=quiet&&movesPassedPawn(m);
            if(!root&&best>-MATE+MAX_PLY&&!inChk&&legal>0){
                if(!urgentPawnRace&&quiet&&depth<=4&&legal>=3+depth*depth)continue; // late move pruning
                if(!urgentPawnRace&&!quiet&&depth<=5&&!m.promotion()&&see(pos,m)<-90*depth)continue;
            }
            Undo u; if(!pos.make(m,u))continue; ++legal; prevMove[ply]=m;
            bool gives=pos.inCheck();
            if(!urgentPawnRace&&!root&&best>-MATE+MAX_PLY&&quiet&&!gives&&!inChk&&depth<=5&&legal>1&&eval+110+105*depth<=alpha&&!pv){pos.undo(m,u);continue;} // futility
            int newDepth=depth-1;
            // Keep advanced passed-pawn pushes and all promotions fully searched.
            if(m.promotion()||(passerMove&&depth<=6)) ++newDepth;
            int s;
            if(legal==1) s=-search(newDepth,-beta,-alpha,ply+1,pv,true);
            else {
                int R=0;
                if(!urgentPawnRace&&depth>=3&&legal>=3&&quiet&&!passerMove){
                    R=lmr[std::min(depth,63)][std::min(legal,63)];
                    if(pv) R--;
                    if(gives) R--;
                    if(improving) R--;
                    if(m==killers[ply][0]||m==killers[ply][1]) R--;
                    R-=std::clamp(history[idx(-pos.side)][m.from()][m.to()]/6000,-2,2);
                    R=std::clamp(R,0,newDepth-1);
                }
                s=-search(newDepth-R,-alpha-1,-alpha,ply+1,false,true);
                if(s>alpha&&R>0)s=-search(newDepth,-alpha-1,-alpha,ply+1,false,true);
                if(s>alpha&&s<beta&&pv)s=-search(newDepth,-beta,-alpha,ply+1,true,true);
            }
            pos.undo(m,u);
            if(sh.stop.load(std::memory_order_relaxed)){ if(root)aborted=true; return 0; }
            if(quiet&&quietCount<64)quiets[quietCount++]=m;
            if(s>best){best=s;
                if(s>alpha){alpha=s;bestMove=m;
                    if(pv){pvTable[ply][0]=m;for(int k=0;k<pvLen[ply+1];k++)pvTable[ply][k+1]=pvTable[ply+1][k];pvLen[ply]=pvLen[ply+1]+1;}
                    if(s>=beta){
                        if(quiet){
                            if(killers[ply][0]!=m){killers[ply][1]=killers[ply][0];killers[ply][0]=m;}
                            int bonus=std::min(depth*depth*32,2000);
                            updateHistory(pos.side,m,bonus);
                            for(int q=0;q<quietCount-1;q++)updateHistory(pos.side,quiets[q],-bonus);
                            if(prev)counter[prev.from()][prev.to()]=m;
                        }
                        break;
                    }
                }
            }
        }
        if(!legal){ if(excluded)return alpha; return inChk?-MATE+ply:0; }
        if(!excluded&&!sh.stop.load(std::memory_order_relaxed)) sh.tt.store(pos.hash,bestMove,best,depth,best>=beta?B_LOWER:(best>origAlpha?B_EXACT:B_UPPER),ply);
        return best;
    }
    std::string pvString(){ std::string s; for(auto m:rootPV)s+=m.uci()+" "; if(!s.empty())s.pop_back(); return s; }
    void report(int depth,int score){
        if(!sh.showInfo)return;
        int64_t t=std::max<int64_t>(1,nowMs()-sh.start); uint64_t n=sh.totalNodes.load()+nodes;
        std::ostringstream o; o<<"info depth "<<depth<<" seldepth "<<selDepth<<" multipv 1 score ";
        if(std::abs(score)>=MATE-MAX_PLY)o<<"mate "<<(score>0?(MATE-score+1)/2:-(MATE+score)/2);
        else o<<"cp "<<score;
        o<<" nodes "<<n<<" nps "<<n*1000/t<<" hashfull "<<sh.tt.hashfull()<<" time "<<t<<" pv "<<pvString();
        std::cout<<o.str()<<std::endl;
    }
    void iterate(int maxDepth) {
        rootSide=pos.side; rootBest=Move(); rootScore=0; int prevScore=0;
        auto legalMoves=pos.legal(); if(legalMoves.empty())return;
        rootBest=legalMoves[0];
        Move previousBest;
        int lastCompletedScore=0;
        int64_t softLimit=sh.softMs;
        for(int d=1;d<=maxDepth&&!sh.stop;d++){
            if(id>0){ int skip=(id+d)%3==0 && d>4; if(skip) continue; }
            selDepth=0; aborted=false; int delta=d>=5?18:INF, a=d>=5?prevScore-delta:-INF, b=d>=5?prevScore+delta:INF, s;
            while(true){
                s=search(d,a,b,0,true,false);
                if(sh.stop||aborted)break;
                if(s<=a){a=std::max(-INF,a-delta);delta+=delta/2+4;if(a<-2000)a=-INF;}
                else if(s>=b){b=std::min(INF,b+delta);delta+=delta/2+4;if(b>2000)b=INF;}
                else break;
            }
            if(aborted&&!pvLen[0])break;
            if(aborted)break;
            if(sh.stop && d>1) break;
            int previousScore=lastCompletedScore;
            prevScore=s; rootScore=s; rootDepth=d;
            if(pvLen[0]){ rootPV.assign(pvTable[0],pvTable[0]+pvLen[0]); rootBest=rootPV[0]; }
            if(id==0){
                report(d,s);
                int64_t elapsed=nowMs()-sh.start;
                int swing=d>1?std::abs(s-previousScore):0;
                bool bestChanged=previousBest && rootBest && rootBest!=previousBest;
                bool critical=pos.inCheck() || swing>=100 || (bestChanged&&swing>=35)
                              || (rootBest&&rootBest.tactical()&&swing>=50);
                // Extend the target in unstable/tactical positions, but never exceed hardMs.
                if(critical && softLimit>0 && sh.hardMs>softLimit){
                    int64_t extension=std::max<int64_t>(softLimit/2,200);
                    softLimit=std::min(sh.hardMs,softLimit+extension);
                }
                if(!sh.pondering&&!sh.limits.infinite&&softLimit>0&&elapsed>=softLimit) sh.stop=true;
                if(sh.limits.depth&&d>=sh.limits.depth&&!sh.limits.infinite&&!sh.pondering) sh.stop=true;
                if(std::abs(s)>=MATE-MAX_PLY && d>=MATE-std::abs(s)+2 && !sh.pondering && !sh.limits.infinite) sh.stop=true;
                previousBest=rootBest;
                lastCompletedScore=s;
            } else {
                lastCompletedScore=s;
                previousBest=rootBest;
            }
        }
    }
};
} // namespace ag
