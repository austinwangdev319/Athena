// Athena 0.1 — original implementation, MIT licensed. See LICENSE.
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace ag {
using Key = uint64_t;
enum Piece { EMPTY=0, PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5, KING=6 };
constexpr int WHITE=1, BLACK=-1;
constexpr int CAPTURE=1<<17, EP=1<<18, CASTLE=1<<19, DOUBLE=1<<20;
constexpr int MAX_PLY=128, MATE=30000, INF=32000;
constexpr int value[7]={0,100,320,335,500,950,20000};
constexpr int knightSteps[8]={33,31,18,14,-14,-18,-31,-33};
constexpr int bishopSteps[4]={17,15,-15,-17};
constexpr int rookSteps[4]={16,1,-1,-16};
constexpr int kingSteps[8]={16,17,1,-15,-16,-17,-1,15};
inline bool valid(int s) { return !(s & 0x88); }
inline int color(int p) { return (p>0)-(p<0); }
inline int idx(int side) { return side==WHITE ? 0:1; }
inline int rankOf(int s) { return s>>4; }
inline int fileOf(int s) { return s&7; }
inline std::string squareName(int s) { return {char('a'+fileOf(s)),char('1'+rankOf(s))}; }
inline int parseSquare(const std::string& s) {
    return s.size()==2 && s[0]>='a' && s[0]<='h' && s[1]>='1' && s[1]<='8'
        ? (s[1]-'1')*16+s[0]-'a' : -1;
}
struct Move {
    uint32_t v=0;
    Move()=default;
    explicit Move(uint32_t x):v(x){}
    Move(int f,int t,int promo=0,int flags=0):v(f|(t<<7)|(promo<<14)|flags){}
    int from() const { return v&127; }
    int to() const { return (v>>7)&127; }
    int promotion() const { return (v>>14)&7; }
    bool capture() const { return v & CAPTURE; }
    bool ep() const { return v & EP; }
    bool castle() const { return v & CASTLE; }
    bool tactical() const { return capture() || promotion(); }
    explicit operator bool() const { return v!=0; }
    bool operator==(Move b) const { return v==b.v; }
    bool operator!=(Move b) const { return v!=b.v; }
    std::string uci() const {
        if (!v) return "0000";
        std::string s=squareName(from())+squareName(to());
        if (promotion()) s += "  nbrqk"[promotion()];
        return s;
    }
};
struct Zobrist {
    Key piece[2][7][128]{}, castle[16]{}, ep[8]{}, side{};
    Zobrist() {
        uint64_t seed=0xA66E550F20261015ULL;
        auto rand=[&]() { uint64_t z=(seed+=0x9e3779b97f4a7c15ULL);
            z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
            z=(z^(z>>27))*0x94d049bb133111ebULL; return z^(z>>31); };
        for (auto& c:piece) for(auto& p:c) for(auto& s:p) s=rand();
        for(auto& x:castle) x=rand();
        for(auto& x:ep) x=rand();
        side=rand();
    }
};
inline const Zobrist Z;
struct Undo {
    int captured=0,captureSq=0,moving=0,castle=0,ep=-1,halfmove=0,fullmove=1;
    int kings[2]{};
    Key hash=0;
};
class Position {
public:
    std::array<int8_t,128> b{};
    int side=WHITE,castle=15,ep=-1,halfmove=0,fullmove=1,kings[2]={4,116};
    Key hash=0;
    std::vector<Key> history;
    Position() { setFen(startFen()); }
    static std::string startFen() { return "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"; }
    static bool attacked(const std::array<int8_t,128>& a,int s,int by) {
        int d=by*16;
        for(int k:{-1,1}) {int t=s-d+k; if(valid(t)&&a[t]==by*PAWN) return true;}
        for(int d0:knightSteps) {int t=s+d0; if(valid(t)&&a[t]==by*KNIGHT) return true;}
        for(int d0:kingSteps) {int t=s+d0; if(valid(t)&&a[t]==by*KING) return true;}
        for(int d0:bishopSteps) for(int t=s+d0;valid(t);t+=d0) if(a[t]) {
            if(a[t]==by*BISHOP||a[t]==by*QUEEN) return true;
            break;
        }
        for(int d0:rookSteps) for(int t=s+d0;valid(t);t+=d0) if(a[t]) {
            if(a[t]==by*ROOK||a[t]==by*QUEEN) return true;
            break;
        }
        return false;
    }
    bool attacked(int s,int by) const { return attacked(b,s,by); }
    bool inCheck() const { return attacked(kings[idx(side)],-side); }
    // Only a legally available en-passant capture changes repetition identity.
    int epFile() const {
        if(ep<0) return -1;
        int captured=ep-side*16;
        if(!valid(captured)||b[captured]!=-side*PAWN) return -1;
        for(int delta:{-1,1}) {
            int f=ep-side*16+delta;
            if(valid(f)&&b[f]==side*PAWN) {
                auto copy=b; copy[f]=0; copy[captured]=0; copy[ep]=side*PAWN;
                if(!attacked(copy,kings[idx(side)],-side)) return fileOf(ep);
            }
        }
        return -1;
    }
    Key computeHash() const {
        Key h=Z.castle[castle];
        if(side==BLACK) h^=Z.side;
        int ef=epFile(); if(ef>=0) h^=Z.ep[ef];
        for(int s=0;s<128;s++) { if(!valid(s)) {s+=7;continue;}
            int p=b[s]; if(p) h^=Z.piece[idx(color(p))][std::abs(p)][s]; }
        return h;
    }
    bool setFen(const std::string& fen) {
        std::istringstream in(fen); std::string placement,turn,rights,target; int hm=0,fm=1;
        if(!(in>>placement>>turn>>rights>>target)) return false;
        if(!(in>>hm>>fm)) {hm=0;fm=1;}
        std::array<int8_t,128> board{}; int r=7,f=0,k[2]={-1,-1},pawns[2]={0,0};
        for(char c:placement) {
            if(c=='/') {if(f!=8||r==0)return false; --r;f=0;continue;}
            if(c>='1'&&c<='8') {f+=c-'0';if(f>8)return false;continue;}
            const std::string names=" PNBRQK"; auto p=names.find(char(std::toupper(static_cast<unsigned char>(c))));
            if(p==std::string::npos||p==0||f>=8) return false;
            int co=std::isupper(static_cast<unsigned char>(c))?WHITE:BLACK;
            int s=r*16+f++; board[s]=int(p)*co;
            if(p==KING) {if(k[idx(co)]!=-1)return false;k[idx(co)]=s;}
            if(p==PAWN) {if(r==0||r==7)return false;++pawns[idx(co)];}
        }
        if(r!=0||f!=8||k[0]<0||k[1]<0||pawns[0]>8||pawns[1]>8) return false;
        if(turn!="w"&&turn!="b") return false;
        int cr=0;
        for(char c:rights) {if(c=='K')cr|=1;else if(c=='Q')cr|=2;else if(c=='k')cr|=4;else if(c=='q')cr|=8;else if(c!='-')return false;}
        int ep0=target=="-"?-1:parseSquare(target);
        if(target!="-"&&(ep0<0||(rankOf(ep0)!=2&&rankOf(ep0)!=5)))return false;
        if(hm<0||fm<1) return false;
        int stm=turn=="w"?WHITE:BLACK;
        if(attacked(board,k[idx(-stm)],stm)) return false; // Non-moving king may not be in check.
        b=board;side=stm;castle=cr;ep=ep0;halfmove=hm;fullmove=fm;kings[0]=k[0];kings[1]=k[1];
        hash=computeHash();history.assign(1,hash);return true;
    }
    std::string fen() const {
        std::string s;const std::string names=" PNBRQK";
        for(int r=7;r>=0;--r) {int empty=0;for(int f=0;f<8;f++) {
            int p=b[r*16+f];if(!p){++empty;continue;}
            if(empty){s+=char('0'+empty);empty=0;}
            char c=names[std::abs(p)];s+=p>0?c:char(std::tolower(c));
        }if(empty)s+=char('0'+empty);if(r)s+='/';}
        s+=side==WHITE?" w ":" b ";
        if(!castle)s+='-';else {if(castle&1)s+='K';if(castle&2)s+='Q';if(castle&4)s+='k';if(castle&8)s+='q';}
        s+=' ';s+=ep<0?"-":squareName(ep);s+=' '+std::to_string(halfmove)+' '+std::to_string(fullmove);return s;
    }
    void pseudo(std::vector<Move>& moves,bool tacticalOnly=false) const {
        moves.clear();moves.reserve(80);
        auto add=[&](int f,int t,int flags=0,int promo=0){moves.emplace_back(f,t,promo,flags);};
        auto pawn=[&](int f,int t,int flags){
            if(rankOf(t)==0||rankOf(t)==7) for(int p:{QUEEN,KNIGHT,ROOK,BISHOP})add(f,t,flags,p);
            else add(f,t,flags);
        };
        for(int s=0;s<128;++s) {
            if(!valid(s)){s+=7;continue;}int p=b[s];if(color(p)!=side)continue;
            int pt=std::abs(p);
            if(pt==PAWN) {
                int d=side*16,t=s+d;
                if(valid(t)&&!b[t]) {
                    if(!tacticalOnly||rankOf(t)==0||rankOf(t)==7)pawn(s,t,0);
                    int home=side==WHITE?1:6;
                    if(!tacticalOnly&&rankOf(s)==home&&!b[s+2*d])add(s,s+2*d,DOUBLE);
                }
                for(int df:{-1,1}) {t=s+d+df;if(!valid(t))continue;
                    if(color(b[t])==-side&&std::abs(b[t])!=KING)pawn(s,t,CAPTURE);
                    if(t==ep&&!b[t]&&b[t-d]==-side*PAWN)add(s,t,CAPTURE|EP);
                }
            } else if(pt==KNIGHT||pt==KING) {
                const int* steps=pt==KNIGHT?knightSteps:kingSteps;
                for(int i=0;i<8;i++){int t=s+steps[i];if(!valid(t)||color(b[t])==side||std::abs(b[t])==KING)continue;
                    if(b[t]||!tacticalOnly)add(s,t,b[t]?CAPTURE:0);}
                if(pt==KING&&!tacticalOnly&&!inCheck()) {
                    int home=side==WHITE?0:112,ks=side==WHITE?1:4,qs=side==WHITE?2:8;
                    if(s==home+4) {
                        if((castle&ks)&&b[home+7]==side*ROOK&&!b[home+5]&&!b[home+6]
                           &&!attacked(home+5,-side)&&!attacked(home+6,-side))add(s,home+6,CASTLE);
                        if((castle&qs)&&b[home]==side*ROOK&&!b[home+1]&&!b[home+2]&&!b[home+3]
                           &&!attacked(home+3,-side)&&!attacked(home+2,-side))add(s,home+2,CASTLE);
                    }
                }
            } else {
                for(int i=0;i<8;i++) {
                    if(pt==BISHOP&&!(i&1))continue; // kingSteps: even orthogonal, odd diagonal
                    if(pt==ROOK&&(i&1))continue;
                    int d=kingSteps[i];
                    for(int t=s+d;valid(t);t+=d) {
                        if(color(b[t])==side||std::abs(b[t])==KING)break;
                        if(b[t]||!tacticalOnly)add(s,t,b[t]?CAPTURE:0);
                        if(b[t])break;
                    }
                }
            }
        }
    }
    void remove(int s) {int p=b[s];if(p)hash^=Z.piece[idx(color(p))][std::abs(p)][s];b[s]=0;}
    void put(int s,int p) {b[s]=p;if(p)hash^=Z.piece[idx(color(p))][std::abs(p)][s];}
    bool make(Move m,Undo& u) {
        int f=m.from(),t=m.to(),us=side,p=b[f];
        u.moving=p;u.captureSq=m.ep()?t-side*16:t;u.captured=b[u.captureSq];
        u.castle=castle;u.ep=ep;u.halfmove=halfmove;u.fullmove=fullmove;u.hash=hash;
        u.kings[0]=kings[0];u.kings[1]=kings[1];
        int ef=epFile();if(ef>=0)hash^=Z.ep[ef];hash^=Z.castle[castle];
        remove(f);remove(u.captureSq);put(t,m.promotion()?us*m.promotion():p);
        if(m.castle()) {int rf=t>f?f+3:f-4,rt=t>f?f+1:f-1;remove(rf);put(rt,us*ROOK);}
        if(std::abs(p)==KING) {kings[idx(us)]=t;castle&=us==WHITE?12:3;}
        if(f==0||t==0) castle&=~2;
        if(f==7||t==7) castle&=~1;
        if(f==112||t==112) castle&=~8;
        if(f==119||t==119) castle&=~4;
        ep=(m.v&DOUBLE)?(f+t)/2:-1;
        halfmove=(std::abs(p)==PAWN||u.captured)?0:halfmove+1;
        if(us==BLACK)++fullmove;
        side=-side;hash^=Z.side;hash^=Z.castle[castle];ef=epFile();if(ef>=0)hash^=Z.ep[ef];
        history.push_back(hash);
        if(attacked(kings[idx(us)],side)){undo(m,u);return false;}return true;
    }
    void undo(Move m,const Undo& u) {
        side=-side;b[m.from()]=u.moving;b[m.to()]=0;b[u.captureSq]=u.captured;
        if(m.castle()) {int f=m.from(),t=m.to(),rf=t>f?f+3:f-4,rt=t>f?f+1:f-1;b[rf]=side*ROOK;b[rt]=0;}
        castle=u.castle;ep=u.ep;halfmove=u.halfmove;fullmove=u.fullmove;hash=u.hash;
        kings[0]=u.kings[0];kings[1]=u.kings[1];history.pop_back();
    }
    void makeNull(Undo& u) {
        u.ep=ep;u.halfmove=halfmove;u.hash=hash;int ef=epFile();if(ef>=0)hash^=Z.ep[ef];
        ep=-1;side=-side;hash^=Z.side;++halfmove;history.push_back(hash);
    }
    void undoNull(const Undo& u) {ep=u.ep;halfmove=u.halfmove;hash=u.hash;side=-side;history.pop_back();}
    std::vector<Move> legal() {
        std::vector<Move> p,out;pseudo(p);
        for(auto m:p) {Undo u;if(make(m,u)){out.push_back(m);undo(m,u);}}return out;
    }
    Move parseMove(const std::string& str) {for(auto m:legal())if(m.uci()==str)return m;return Move();}
    bool repetition(int required=2) const {
        int hits=1,limit=std::min(halfmove,int(history.size())-1);
        for(int d=2;d<=limit;d+=2)if(history[history.size()-1-d]==hash&&++hits>=required)return true;
        return false;
    }
    bool insufficient() const {
        int minors=0,knights=0,bishopsColor=-1;bool mixed=false;
        for(int s=0;s<128;s++){if(!valid(s)){s+=7;continue;}int p=std::abs(b[s]);
            if(p==PAWN||p==ROOK||p==QUEEN)return false;
            if(p==KNIGHT){++minors;++knights;}
            if(p==BISHOP){++minors;int c=(fileOf(s)+rankOf(s))&1;if(bishopsColor<0)bishopsColor=c;else if(c!=bishopsColor)mixed=true;}
        }
        return minors<=1 || (knights==0&&!mixed);
    }
    int nonPawnMaterial(int who) const {
        int n=0;for(int s=0;s<128;s++){if(!valid(s)){s+=7;continue;}int p=b[s];
            if(color(p)==who&&std::abs(p)>PAWN&&std::abs(p)<KING)n+=value[std::abs(p)];}return n;
    }
};
inline uint64_t perft(Position& p,int depth) {
    if(!depth)return 1;
    std::vector<Move> moves;p.pseudo(moves);uint64_t n=0;
    for(Move m:moves){Undo u;if(p.make(m,u)){n+=depth==1?1:perft(p,depth-1);p.undo(m,u);}}return n;
}
} // namespace ag
