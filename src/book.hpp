// Athena opening book.
//
// Where the "database" lives: there is no online database. The repertoire is a plain list of opening lines.
//   1. A default copy is compiled into the executable (src/gambits.inc), so the engine works as a single file.
//   2. Optionally, the UCI option BookFile points at a text file (book/athena_gambits.txt) that is loaded at
//      startup and ADDED to the built-in lines. Edit that file to change what Athena plays; no rebuild needed.
//
// File format, one line per opening, '#' starts a comment:
//     <weight> <deficit_cp> <w|b> <uci moves from the start position>
//     12 22 w e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4
#pragma once
#include "chess.hpp"
#include <fstream>
#include <random>

namespace ag {
struct BookLine { int weight; int deficit; int side; std::string moves; };
struct BookEntry { int weight, deficit, side; std::vector<Move> moves; };

inline const BookLine BUILTIN_BOOK[] = {
#include "gambits.inc"
#include "standard_book.inc"
};

inline bool makeEntry(int weight,int deficit,int side,const std::string& moves,BookEntry& out) {
    Position p; p.setFen(Position::startFen()); std::istringstream in(moves); std::string s;
    out.weight=weight; out.deficit=deficit; out.side=side; out.moves.clear();
    while(in>>s) { Move m=p.parseMove(s); if(!m) return false; Undo u; p.make(m,u); out.moves.push_back(m); }
    return !out.moves.empty() && weight>0;
}
inline std::vector<BookEntry> builtinBook() {
    std::vector<BookEntry> v; for(const auto& l:BUILTIN_BOOK){ BookEntry e; if(makeEntry(l.weight,l.deficit,l.side,l.moves,e)) v.push_back(e);} return v;
}
inline int loadBookFile(const std::string& path,std::vector<BookEntry>& book,int* skipped=nullptr) {
    std::ifstream f(path); if(!f) return -1; std::string line; int ok=0,bad=0;
    while(std::getline(f,line)) {
        auto h=line.find('#');
        if(h!=std::string::npos) line.erase(h);
        std::istringstream in(line); int w,d; std::string side;
        if(!(in>>w>>d>>side)) continue;
        std::string rest; std::getline(in,rest); BookEntry e;
        if((side=="w"||side=="b") && makeEntry(w,d,side=="w"?WHITE:BLACK,rest,e)) { book.push_back(e); ++ok; }
        else ++bad;
    }
    if(skipped) *skipped=bad;
    return ok;
}

// Pick a book continuation for the current game, or none once we have left the repertoire.
template<class Rng>
Move bookMove(const Position& cur,const std::vector<BookEntry>& book,int maxPly,int maxDeficit,Rng& rng) {
    Position start; start.setFen(Position::startFen());
    const auto& hist=cur.history;
    if(hist.empty()||hist[0]!=start.hash) return Move();
    int played=int(hist.size())-1; if(played>=maxPly) return Move();
    // Against 1.e4 c5, keep Athena's signature Wing Gambit to seize central tempo.
    if(cur.side==WHITE && played==2 &&
       cur.b[parseSquare("e4")]==WHITE*PAWN &&
       cur.b[parseSquare("c5")]==BLACK*PAWN) {
        Position legalPosition=cur;
        Move wing=legalPosition.parseMove("b2b4"); if(wing) return wing;
    }
    std::vector<std::pair<Move,int>> cands;
    for(const auto& line:book) {
        if(line.deficit>maxDeficit||line.side!=cur.side||int(line.moves.size())<=played) continue;
        Position p; p.setFen(Position::startFen()); bool ok=true;
        for(int n=0;n<played;n++){ Undo u; if(!p.make(line.moves[n],u)||p.hash!=hist[n+1]){ok=false;break;} }
        if(!ok) continue;
        Move m=line.moves[played]; bool found=false;
        for(auto& c:cands) if(c.first==m){c.second+=line.weight;found=true;}
        if(!found) cands.push_back({m,line.weight});
    }
    if(cands.empty()) return Move();
    int total=0; for(auto& c:cands) total+=c.second;
    int r=int(rng()%uint64_t(total)); for(auto& c:cands){ if((r-=c.second)<0) return c.first; }
    return cands[0].first;
}
} // namespace ag
