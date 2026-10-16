// Hand-crafted evaluation, tapered between middlegame and endgame.
// "aggression" scales the king-attack and initiative terms (100 = neutral).
#pragma once
#include "chess.hpp"

namespace ag {
struct EvalParams {
    int aggression = 90;    // 100 = neutral; lower values discourage premature king attacks
    int contempt   = 25;    // centipawns; draws are scored slightly bad for the engine
};
inline EvalParams params;

struct Tables {
    int mg[7][128]{}, eg[7][128]{};
    int shield[128]{};
    Tables() {
        static const int pawnMgR[8]={0,-2,-4,6,16,30,45,0}, pawnEgR[8]={0,0,4,12,26,48,80,0};
        for(int s=0;s<128;s++) {
            if(!valid(s)) continue;
            int r=rankOf(s), f=fileOf(s);
            int cd=std::min(f,7-f)+std::min(r,7-r);        // 0 (corner) .. 6 (centre)
            int cf=std::min(f,7-f);                          // 0 edge .. 3 centre
            mg[PAWN][s]=pawnMgR[r]+((f==3||f==4)?(r<=4?8*(r>=3?2:1):4):0)-((f==0||f==7)?6:0);
            eg[PAWN][s]=pawnEgR[r]-((f==0||f==7)?4:0);
            mg[KNIGHT][s]=-34+9*cd+(r>=3&&r<=5?4:0); eg[KNIGHT][s]=-30+7*cd;
            mg[BISHOP][s]=-16+4*cd+(r==0?-6:0);         eg[BISHOP][s]=-12+3*cd;
            mg[ROOK][s]=(cf>=2?4:0)+(r==6?10:0)+(r==0&&(f==3||f==4)?5:0); eg[ROOK][s]=(r==6?10:0)+(r==0?-2:0);
            mg[QUEEN][s]=-8+2*cd;               eg[QUEEN][s]=-14+4*cd;
            int kmg=(r==0?18:-26*r); if(r==0&&(f==6||f==1||f==2)) kmg+=14; if(r==0&&(f==3||f==4)) kmg-=22;
            mg[KING][s]=kmg; eg[KING][s]=-30+10*cd+(r>=2?6:0);
        }
    }
};
inline const Tables T;

inline int mirror(int s) { return s^0x70; }

// Returns score from the side to move's point of view.
inline int evaluate(const Position& pos) {
    const int A=params.aggression;
    int mgS[2]={0,0}, egS[2]={0,0}, phase=0;
    int pawnFile[2][10]={}, pawnMin[2][10], pawnMax[2][10];
    for(int c=0;c<2;c++) for(int f=0;f<10;f++){pawnMin[c][f]=8;pawnMax[c][f]=-1;}
    int pawnCount[2]={0,0}, bishops[2]={0,0}, bishopSq[2]={-1,-1}, minors[2]={0,0};
    int knights[2]={0,0}, rooks[2]={0,0}, queens[2]={0,0};
    bool pawnAtt[2][128]={};
    int attackCount[2][128]={};
    int centerControl[2]={0,0};
    auto centralSquare=[](int s){ int f=fileOf(s), r=rankOf(s); return f>=2&&f<=5&&r>=2&&r<=5; };
    // first pass: pawns
    for(int s=0;s<128;s++){ if(!valid(s)){s+=7;continue;} int p=pos.b[s]; if(std::abs(p)!=PAWN) continue;
        int c=idx(color(p)), f=fileOf(s)+1, r=c==0?rankOf(s):7-rankOf(s);
        pawnFile[c][f]++; pawnMin[c][f]=std::min(pawnMin[c][f],r); pawnMax[c][f]=std::max(pawnMax[c][f],r); pawnCount[c]++;
        int d=color(p)*16; for(int df:{-1,1}){int t=s+d+df; if(valid(t)){pawnAtt[c][t]=true;attackCount[c][t]++;}}
    }
    // Pawn attacks help claim the central squares and their immediate approaches.
    for(int c=0;c<2;c++)
        for(int r=2;r<=5;r++) for(int f=2;f<=5;f++)
            if(pawnAtt[c][r*16+f]) centerControl[c]+=2;
    int kingSq[2]={pos.kings[0],pos.kings[1]};
    int attackUnits[2]={0,0}, attackers[2]={0,0};
    // king zones (squares attacked/near king), by the defender colour
    bool zone[2][128]={};
    for(int c=0;c<2;c++){ int ks=kingSq[c], fw=c==0?16:-16;
        for(int d0:kingSteps){int t=ks+d0; if(valid(t))zone[c][t]=true;}
        zone[c][ks]=true;
        for(int d0:{-1,0,1}){int t=ks+2*fw+d0; if(valid(t))zone[c][t]=true;}
    }
    int development[2]={0,0};
    for(int s=0;s<128;s++){ if(!valid(s)){s+=7;continue;} int p=pos.b[s]; if(!p) continue;
        int pt=std::abs(p), col=color(p), c=idx(col), e=1-c; int rs=col==WHITE?s:mirror(s);
        int base=value[pt]; if(pt==KING) base=0;
        mgS[c]+=base+T.mg[pt][rs]; egS[c]+=base+T.eg[pt][rs];
        if(pt==PAWN){
            int f=fileOf(s)+1, r=c==0?rankOf(s):7-rankOf(s);
            bool isolated = !pawnFile[c][f-1] && !pawnFile[c][f+1];
            if(isolated){mgS[c]-=12;egS[c]-=16;}
            if(pawnFile[c][f]>1){mgS[c]-=8;egS[c]-=14;}
            // passed?
            bool passed=true; int fw=col*16;
            for(int df=-1;df<=1&&passed;df++){ int ff=f+df; if(ff<1||ff>8)continue;
                // any enemy pawn ahead on this file (enemy rank measured from enemy side: ahead means enemy r_e > 7-r... use squares)
                for(int t=s+fw+df; valid(t); t+=fw) if(pos.b[t]==-col*PAWN){passed=false;break;}
            }
            if(passed){
                static const int pm[8]={0,5,10,20,38,66,110,0};
                static const int pe[8]={0,8,16,34,64,110,180,0};
                mgS[c]+=pm[r];
                egS[c]+=pe[r];

                // A passed pawn becomes urgent when the promotion route is clear.
                // Judge the race by the kings' distances to the promotion square and
                // whether the opponent gets a move before the pawn advances again.
                const int promotionRank = col==WHITE ? 7 : 0;
                const int promotionSquare = promotionRank*16 + fileOf(s);
                const int stepsToPromotion = 7-r;
                bool clearPath=true;
                for(int t=s+fw;valid(t);t+=fw) {
                    if(pos.b[t]) { clearPath=false; break; }
                    if(rankOf(t)==promotionRank) break;
                }
                auto kingDistance=[](int a,int b) {
                    return std::max(std::abs(fileOf(a)-fileOf(b)),
                                    std::abs(rankOf(a)-rankOf(b)));
                };
                const int friendlyKingDistance=kingDistance(kingSq[c],promotionSquare);
                const int enemyKingDistance=kingDistance(kingSq[e],promotionSquare);
                if(clearPath) {
                    egS[c]+=std::clamp((enemyKingDistance-friendlyKingDistance)*8,-24,32);
                    const int enemyTempo = pos.side==col ? 0 : 1;
                    if(enemyKingDistance > stepsToPromotion + enemyTempo) {
                        egS[c] += stepsToPromotion<=2 ? 150 : 75;
                    } else if(stepsToPromotion<=2) {
                        // Even a blockable pawn on the sixth/seventh rank demands attention.
                        egS[c] += 35;
                    }
                }
            }
            // Reward advanced central pawns that claim space without making material irrelevant.
            if(f==4||f==5){
                if(r>=3&&r<=5) mgS[c]+=12;
                else if(r>=2) mgS[c]+=6;
                if(r>=3) egS[c]+=4;
            }
            // protected pawn
            if(pawnAtt[c][s]){mgS[c]+=5;egS[c]+=6;}
            continue;
        }
        if(pt==KING){ for(int d0:kingSteps){int t=s+d0;if(valid(t))attackCount[c][t]++;} continue; }
        // piece activity
        int mob=0, ksAtt=0; bool attacksZone=false;
        auto touch=[&](int t){
            attackCount[c][t]++;
            if(zone[e][t]){attacksZone=true;ksAtt++;}
            if(centralSquare(t)) centerControl[c]++;
        };
        if(pt==KNIGHT){ minors[c]++; knights[c]++; phase+=1;
            for(int d0:knightSteps){int t=s+d0; if(!valid(t))continue; if(color(pos.b[t])!=col&&!pawnAtt[e][t])mob++; touch(t);}
            mgS[c]+=(mob-4)*4; egS[c]+=(mob-4)*4;
            // outpost
            int r=c==0?rankOf(s):7-rankOf(s);
            if(r>=3&&r<=5&&pawnAtt[c][s]&&!pawnAtt[e][s]){
                mgS[c]+=22; egS[c]+=12;
                if(fileOf(s)==3||fileOf(s)==4){mgS[c]+=8;egS[c]+=4;}
            }
            if(r>=1 && (c==0?rankOf(s)>0:rankOf(s)<7)) development[c]++;
            }
        else if(pt==BISHOP){ minors[c]++; bishops[c]++; bishopSq[c]=s; phase+=1;
            for(int d0:bishopSteps) for(int t=s+d0;valid(t);t+=d0){ if(color(pos.b[t])!=col&&!pawnAtt[e][t])mob++; touch(t); if(pos.b[t])break; }
            mgS[c]+=(mob-6)*5; egS[c]+=(mob-6)*5;
            if(c==0?rankOf(s)>0:rankOf(s)<7) development[c]++; }
        else if(pt==ROOK){ rooks[c]++; phase+=2;
            for(int d0:rookSteps) for(int t=s+d0;valid(t);t+=d0){ if(color(pos.b[t])!=col)mob++; touch(t); if(pos.b[t])break; }
            mgS[c]+=(mob-7)*2; egS[c]+=(mob-7)*4;
            int f=fileOf(s)+1;
            if(!pawnFile[c][f]){ mgS[c]+= pawnFile[e][f]?12:24; egS[c]+= pawnFile[e][f]?6:12;
                // pressure on enemy king file
                if(std::abs(fileOf(kingSq[e])-fileOf(s))<=1){mgS[c]+=10*A/100;} } }
        else if(pt==QUEEN){ queens[c]++; phase+=4;
            for(int d0:kingSteps) for(int t=s+d0;valid(t);t+=d0){ if(color(pos.b[t])!=col)mob++; touch(t); if(pos.b[t])break; }
            mgS[c]+=(mob-14); egS[c]+=(mob-14)*2; }
        if(attacksZone){ attackers[c]++; static const int w[7]={0,0,2,2,3,5,0};
            attackUnits[c]+=w[pt]*ksAtt+w[pt]; }
    }
    // Penalize loose material: attacks on an undefended piece create immediate tactical
    // obligations. The penalty is deliberately smaller than the piece's material value
    // because static attack maps do not account for pins or forcing continuations.
    for(int s=0;s<128;s++) {
        if(!valid(s)){s+=7;continue;}
        int p=pos.b[s];
        if(!p || std::abs(p)==KING) continue;
        int c=idx(color(p)), e=1-c, pt=std::abs(p);
        if(attackCount[e][s] > attackCount[c][s]) {
            int relativeRank = c==0 ? rankOf(s) : 7-rankOf(s);
            int risk = pt==PAWN ? (relativeRank>=4 ? 36 : 28)
                                : std::min(90,value[pt]/5);
            mgS[c]-=risk;
            egS[c]-=risk/2;
        }
    }
    // Central control and space are useful strategic goals, capped to preserve tactical/material balance.
    for(int c=0;c<2;c++) mgS[c] += std::min(centerControl[c],16)*2;
    phase=std::min(phase,24);
    // king safety and attack
    int kingMg[2]={0,0};
    for(int c=0;c<2;c++){ int e=1-c, ks=kingSq[e]; // c attacks enemy king e
        int n=attackers[c];
        // King attacks should be earned through coordinated piece activity, not used as
        // compensation for speculative pawn losses. Three attackers receive a modest
        // bonus; two attackers with a queen receive only a small setup bonus.
        int attackBonus=0;
        if(n>=3) {
            int u=std::min(attackUnits[c],30);
            attackBonus=(u*u)/12 + 2*u;
            if(queens[c] && n>=4) attackBonus+=10;
        } else if(n>=2 && queens[c]) {
            int u=std::min(attackUnits[c],20);
            attackBonus=std::min(40,2*u);
        }
        if(development[c]<3) attackBonus=attackBonus*2/3;
        kingMg[c]+=attackBonus*A/100;
        // pawn shield for king e
        int sh=0, fw=e==0?16:-16; int kf=fileOf(ks);
        for(int df=-1;df<=1;df++){ int f=kf+df; if(f<0||f>7)continue;
            int t1=ks+fw+df, t2=ks+2*fw+df; int own=e==0?WHITE:BLACK;
            bool p1=valid(t1)&&pos.b[t1]==own*PAWN, p2=valid(t2)&&pos.b[t2]==own*PAWN;
            if(p1)sh+=0; else if(p2)sh-=8; else sh-=18;
            if(!pawnFile[e][f+1]) sh-= pawnFile[c][f+1]? 12:22; }
        // credit (to attacker c) is the negative of shield
        if(n>=2) kingMg[c] += std::clamp(-sh,0,45)*A/100 / (queens[c]?1:2);
    }
    // development / initiative in the opening (gambit compensation)
    int devEdge=0;
    if(phase>=14){
        int lead=development[0]-development[1];
        devEdge=lead*10*A/100;
        for(int c=0;c<2;c++){ int hr=c==0?0:112; int ks=kingSq[c];
            bool stuck = (ks==hr+4) && !(pos.castle & (c==0?3:12));
            if(stuck){ int pen=22*A/100; devEdge += c==0?-pen:pen; }
        }
    }
    // Penalize early queen sorties while minor pieces are still at home.
    for(int c=0;c<2;c++){ if(phase<18) break; int home=c==0?3:115; int qs=-1;
        for(int t=0;t<128;t++){ if(!valid(t)){t+=7;continue;} if(pos.b[t]==(c==0?QUEEN:-QUEEN)){qs=t;break;} }
        if(qs>=0&&qs!=home&&development[c]<3){ int pen=14*(3-development[c]); devEdge+= c==0?-pen:pen; } }
    // bishop pair & dead positions
    int mgW=mgS[0]-mgS[1]+kingMg[0]-kingMg[1]+devEdge, egW=egS[0]-egS[1];
    if(bishops[0]>=2){mgW+=28;egW+=48;} if(bishops[1]>=2){mgW-=28;egW-=48;}
    // pieces defended-ness / hanging heuristic skipped; search handles tactics
    int score=(mgW*phase+egW*(24-phase))/24;
    // simple endgame knowledge
    int w=pos.nonPawnMaterial(WHITE), bl=pos.nonPawnMaterial(BLACK);
    if(!pawnCount[0] && !pawnCount[1]){
        if(w==0 && bl<=value[BISHOP]+10) score=0 + (score/8);
        if(bl==0 && w<=value[BISHOP]+10) score=0 + (score/8);
    }
    // opposite-coloured bishops drawish
    if(bishops[0]==1&&bishops[1]==1&&knights[0]+knights[1]+rooks[0]+rooks[1]+queens[0]+queens[1]==0){
        int cw=(fileOf(bishopSq[0])+rankOf(bishopSq[0]))&1, cb=(fileOf(bishopSq[1])+rankOf(bishopSq[1]))&1;
        if(cw!=cb) score=score*5/8;
    }
    // Mop-up: drive the lone king to the edge and bring our king close
    auto mopUp=[&](int strong,int weakKing,int strongKing){
        int cd=std::min(fileOf(weakKing),7-fileOf(weakKing))+std::min(rankOf(weakKing),7-rankOf(weakKing));
        int dist=std::abs(fileOf(weakKing)-fileOf(strongKing))+std::abs(rankOf(weakKing)-rankOf(strongKing));
        return strong*(60-18*cd+ (14-dist)*6);
    };
    if(bl==0&&!pawnCount[1]&&w>=value[ROOK]) score+=mopUp(1,kingSq[1],kingSq[0]);
    if(w==0&&!pawnCount[0]&&bl>=value[ROOK]) score+=mopUp(-1,kingSq[0],kingSq[1]);
    if(pos.halfmove>=80) score=score*(100-(pos.halfmove-80))/100;
    return (pos.side==WHITE?score:-score)+12;
}
} // namespace ag
