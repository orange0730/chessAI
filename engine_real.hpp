#pragma once
#include <array>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdint>

enum Piece : int {
    EMPTY = 0,
    WP, WN, WB, WR, WQ, WK,
    BP, BN, BB, BR, BQ, BK
};
inline bool isWhite(Piece p){ return p>=WP && p<=WK; }
inline bool isBlack(Piece p){ return p>=BP && p<=BK; }

struct Move {
    int from=0, to=0;
    Piece promo=EMPTY;
    Piece captured=EMPTY;
};

struct Undo {
    Piece captured=EMPTY;
    int halfmoveClock=0;
    int epSq=-1;
    uint8_t castle=0;

    // 用來還原特殊著法
    bool wasEP=false;
    int epCapturedSq=-1;

    bool wasCastle=false;
    int rookFrom=-1, rookTo=-1;
    Piece rookPiece=EMPTY;

    Piece movedPiece=EMPTY; // 起點那顆（升變前）
};

struct Weights {
    double material[6]{100,320,330,500,900,0}; // P,N,B,R,Q,K
    double pstPawn[64]{0};
    double pstKnight[64]{0};

    static Weights defaultWeights(){ return Weights(); }

    bool load(const std::string& path){
        std::ifstream in(path);
        if(!in) return false;
        for(int i=0;i<6;i++) in>>material[i];
        for(int i=0;i<64;i++) in>>pstPawn[i];
        for(int i=0;i<64;i++) in>>pstKnight[i];
        return true;
    }
    bool save(const std::string& path) const{
        std::ofstream out(path);
        if(!out) return false;
        for(int i=0;i<6;i++){ if(i) out<<' '; out<<material[i]; }
        out<<"\n";
        for(int i=0;i<64;i++){ if(i) out<<' '; out<<pstPawn[i]; }
        out<<"\n";
        for(int i=0;i<64;i++){ if(i) out<<' '; out<<pstKnight[i]; }
        out<<"\n";
        return true;
    }
};

struct Position {
    std::array<Piece,64> b{};
    bool whiteToMove=true;
    int halfmoveClock=0;

    // en passant 目標格（例如白兵 e2->e4，epSq = e3）
    int epSq=-1;

    // castling rights bitmask: 1=WK,2=WQ,4=BK,8=BQ
    uint8_t castle=0;

    static int fileOf(int sq){ return sq & 7; }
    static int rankOf(int sq){ return sq >> 3; }
    static bool onBoard(int sq){ return sq>=0 && sq<64; }
    static int xyToSq(int x,int y){ return y*8+x; }

    void setStartPos(){
        b.fill(EMPTY);
        for(int f=0;f<8;f++){ b[8+f]=WP; b[48+f]=BP; }
        b[0]=WR; b[7]=WR; b[56]=BR; b[63]=BR;
        b[1]=WN; b[6]=WN; b[57]=BN; b[62]=BN;
        b[2]=WB; b[5]=WB; b[58]=BB; b[61]=BB;
        b[3]=WQ; b[59]=BQ;
        b[4]=WK; b[60]=BK;

        whiteToMove=true;
        halfmoveClock=0;
        epSq=-1;
        castle = 1|2|4|8; // KQkq
    }

    // 解析標準 FEN（piece placement / active color / castling / ep / halfmove / fullmove）
    void setFEN(const std::string& fen){
        std::fill(b.begin(), b.end(), EMPTY);
        whiteToMove = true;
        halfmoveClock = 0;
        epSq = -1;
        castle = 0;

        std::istringstream ss(fen);
        std::string board, active, castlingStr, epStr, halfStr, fullStr;
        if(!(ss >> board >> active >> castlingStr >> epStr >> halfStr >> fullStr)){
            // FEN 格式不完整就退回起始局面，避免盤面亂掉
            setStartPos();
            return;
        }

        // 1) 棋子佈局
        int rank = 7;
        int file = 0;
        for(char c : board){
            if(c == '/'){
                rank--;
                file = 0;
                continue;
            }
            if(c >= '1' && c <= '8'){
                file += c - '0';
                continue;
            }
            if(rank < 0 || file < 0 || file >= 8) continue;
            Piece p = EMPTY;
            switch(c){
                case 'P': p = WP; break;
                case 'N': p = WN; break;
                case 'B': p = WB; break;
                case 'R': p = WR; break;
                case 'Q': p = WQ; break;
                case 'K': p = WK; break;
                case 'p': p = BP; break;
                case 'n': p = BN; break;
                case 'b': p = BB; break;
                case 'r': p = BR; break;
                case 'q': p = BQ; break;
                case 'k': p = BK; break;
                default:  p = EMPTY; break;
            }
            if(p != EMPTY){
                int sq = rank*8 + file;
                if(onBoard(sq)) b[sq] = p;
            }
            file++;
        }

        // 2) 行棋方
        whiteToMove = (active == "w");

        // 3) 王車易位權
        castle = 0;
        if(castlingStr != "-"){
            for(char c : castlingStr){
                if(c == 'K') castle |= 1;
                if(c == 'Q') castle |= 2;
                if(c == 'k') castle |= 4;
                if(c == 'q') castle |= 8;
            }
        }

        // 4) 吃過路兵目標格
        epSq = -1;
        if(epStr != "-" && epStr.size() == 2){
            int fileEp = epStr[0] - 'a';
            int rankEp = epStr[1] - '1';
            if(fileEp >= 0 && fileEp < 8 && rankEp >= 0 && rankEp < 8){
                epSq = xyToSq(fileEp, rankEp);
            }
        }

        // 5) 半步計數（和局 50 步規則）
        try{
            halfmoveClock = std::stoi(halfStr);
        }catch(...){
            halfmoveClock = 0;
        }
    }

    int findKingSq(bool white) const{
        Piece k = white ? WK : BK;
        for(int i=0;i<64;i++) if(b[i]==k) return i;
        return -1;
    }

    bool squareAttacked(int targetSq, bool byWhite) const{
        // Pawn attacks
        if(byWhite){
            // 白兵從 (x±1, y-1) 攻擊 target
            int x=fileOf(targetSq), y=rankOf(targetSq);
            int y2=y-1;
            if(y2>=0){
                if(x-1>=0){
                    int sq=xyToSq(x-1,y2);
                    if(b[sq]==WP) return true;
                }
                if(x+1<8){
                    int sq=xyToSq(x+1,y2);
                    if(b[sq]==WP) return true;
                }
            }
        }else{
            // 黑兵從 (x±1, y+1) 攻擊 target
            int x=fileOf(targetSq), y=rankOf(targetSq);
            int y2=y+1;
            if(y2<8){
                if(x-1>=0){
                    int sq=xyToSq(x-1,y2);
                    if(b[sq]==BP) return true;
                }
                if(x+1<8){
                    int sq=xyToSq(x+1,y2);
                    if(b[sq]==BP) return true;
                }
            }
        }

        // Knight attacks
        static const int nd[8]={+17,+15,+10,+6,-6,-10,-15,-17};
        for(int dv:nd){
            int sq=targetSq+dv;
            if(!onBoard(sq)) continue;
            int tf=fileOf(targetSq), sf=fileOf(sq);
            int tr=rankOf(targetSq), sr=rankOf(sq);
            if(std::max(std::abs(tf-sf), std::abs(tr-sr))!=2) continue;
            Piece p=b[sq];
            if(byWhite && p==WN) return true;
            if(!byWhite && p==BN) return true;
        }

        // Bishop/Queen diagonal
        {
            for(int dv: {+9,+7,-7,-9}){
                int cur=targetSq;
                while(true){
                    int prev=cur;
                    cur += dv;
                    if(!onBoard(cur)) break;
                    int pf=fileOf(prev), cf=fileOf(cur);
                    if(std::abs(cf-pf)>1) break;
                    Piece p=b[cur];
                    if(p==EMPTY) continue;
                    if(byWhite){
                        if(p==WB || p==WQ) return true;
                    }else{
                        if(p==BB || p==BQ) return true;
                    }
                    break;
                }
            }
        }
        // Rook/Queen straight
        {
            for(int dv: {+8,-8,+1,-1}){
                int cur=targetSq;
                while(true){
                    int prev=cur;
                    cur += dv;
                    if(!onBoard(cur)) break;
                    int pf=fileOf(prev), cf=fileOf(cur);
                    if((dv==1||dv==-1) && std::abs(cf-pf)>1) break;
                    Piece p=b[cur];
                    if(p==EMPTY) continue;
                    if(byWhite){
                        if(p==WR || p==WQ) return true;
                    }else{
                        if(p==BR || p==BQ) return true;
                    }
                    break;
                }
            }
        }

        // King attacks (adjacent)
        static const int kd[8]={+8,-8,+1,-1,+9,+7,-7,-9};
        for(int dv:kd){
            int sq=targetSq+dv;
            if(!onBoard(sq)) continue;
            int tf=fileOf(targetSq), sf=fileOf(sq);
            if(std::abs(tf-sf)>1) continue;
            Piece p=b[sq];
            if(byWhite && p==WK) return true;
            if(!byWhite && p==BK) return true;
        }

        return false;
    }

    bool isInCheck(bool white) const{
        int ksq=findKingSq(white);
        if(ksq<0) return false;
        return squareAttacked(ksq, !white);
    }

    void makeMove(const Move& m, Undo& u){
        u.captured = b[m.to];
        u.halfmoveClock = halfmoveClock;
        u.epSq = epSq;
        u.castle = castle;
        u.wasEP=false; u.epCapturedSq=-1;
        u.wasCastle=false; u.rookFrom=-1; u.rookTo=-1; u.rookPiece=EMPTY;

        Piece p = b[m.from];
        u.movedPiece = p;

        // reset ep by default
        epSq = -1;

        // halfmove clock reset
        if(u.captured!=EMPTY || p==WP || p==BP) halfmoveClock=0;
        else halfmoveClock++;

        // handle en passant capture
        if((p==WP || p==BP) && m.to==u.epSq && b[m.to]==EMPTY){
            // pawn moved into ep target square; capture pawn behind
            u.wasEP=true;
            int capSq = (p==WP) ? (m.to-8) : (m.to+8);
            u.epCapturedSq = capSq;
            u.captured = b[capSq];
            b[capSq]=EMPTY;
        }

        // safety: 若兵是斜走，但目標格既不是 EP 也沒有敵子，視為不應存在的棋譜
        if((p==WP || p==BP)){
            int fromFile = fileOf(m.from);
            int toFile   = fileOf(m.to);
            bool diagonal = std::abs(fromFile - toFile) == 1;
            bool forward  = (p==WP) ? (m.to - m.from == +7 || m.to - m.from == +9 || m.to - m.from == +8 || m.to - m.from == +16)
                                    : (m.from - m.to == +7 || m.from - m.to == +9 || m.from - m.to == +8 || m.from - m.to == +16);
            if(diagonal){
                bool epCapture = u.wasEP;
                bool hasEnemy = (u.captured != EMPTY) && (isWhite(p) != isWhite(u.captured));
                if(!epCapture && !hasEnemy){
                    // 不合法的兵斜走，直接返回不改盤面，避免盤面繼續偏離
                    // （理論上 genLegal 不會生成，防禦外部錯誤輸入）
                    return;
                }
            }
        }

        // move piece
        b[m.from]=EMPTY;
        Piece put = (m.promo!=EMPTY) ? m.promo : p;
        b[m.to]=put;

        // set ep target on double pawn push
        if(p==WP){
            if(m.from/8==1 && m.to/8==3 && b[m.to]==WP){
                epSq = m.from + 8;
            }
        }else if(p==BP){
            if(m.from/8==6 && m.to/8==4 && b[m.to]==BP){
                epSq = m.from - 8;
            }
        }

        // update castling rights when king/rook moves or rook captured
        auto clearRight = [&](uint8_t mask){ castle = (uint8_t)(castle & ~mask); };

        if(p==WK){ clearRight(1|2); }
        if(p==BK){ clearRight(4|8); }

        if(p==WR){
            if(m.from==0) clearRight(2); // a1 rook -> lose WQ
            if(m.from==7) clearRight(1); // h1 rook -> lose WK
        }
        if(p==BR){
            if(m.from==56) clearRight(8); // a8 rook -> lose BQ
            if(m.from==63) clearRight(4); // h8 rook -> lose BK
        }

        // rook captured on corner squares
        if(u.captured==WR){
            if(m.to==0) clearRight(2);
            if(m.to==7) clearRight(1);
        }
        if(u.captured==BR){
            if(m.to==56) clearRight(8);
            if(m.to==63) clearRight(4);
        }

        // handle castling rook move (king moves 2 squares)
        if(p==WK && m.from==4 && (m.to==6 || m.to==2)){
            u.wasCastle=true;
            if(m.to==6){
                // white O-O: rook h1->f1
                u.rookFrom=7; u.rookTo=5; u.rookPiece=b[5];
                b[5]=WR; b[7]=EMPTY;
            }else{
                // white O-O-O: rook a1->d1
                u.rookFrom=0; u.rookTo=3; u.rookPiece=b[3];
                b[3]=WR; b[0]=EMPTY;
            }
        }
        if(p==BK && m.from==60 && (m.to==62 || m.to==58)){
            u.wasCastle=true;
            if(m.to==62){
                // black O-O: rook h8->f8
                u.rookFrom=63; u.rookTo=61; u.rookPiece=b[61];
                b[61]=BR; b[63]=EMPTY;
            }else{
                // black O-O-O: rook a8->d8
                u.rookFrom=56; u.rookTo=59; u.rookPiece=b[59];
                b[59]=BR; b[56]=EMPTY;
            }
        }

        whiteToMove = !whiteToMove;
    }

    void unmakeMove(const Move& m, const Undo& u){
        whiteToMove = !whiteToMove;
        halfmoveClock = u.halfmoveClock;
        epSq = u.epSq;
        castle = u.castle;

        // undo castling rook move first
        if(u.wasCastle){
            // king already moved back below; restore rook
            if(u.rookFrom!=-1 && u.rookTo!=-1){
                b[u.rookFrom] = (isWhite(u.movedPiece)?WR:BR);
                b[u.rookTo] = EMPTY;
            }
        }

        // move piece back
        Piece moved = u.movedPiece; // original (pre-promo)
        b[m.from] = moved;

        // restore capture square
        b[m.to] = u.captured;

        // undo en passant capture (restore captured pawn on its square)
        if(u.wasEP){
            int capSq = u.epCapturedSq;
            b[capSq] = u.captured;
            b[m.to] = EMPTY; // ep target square becomes empty after unmake
        }
    }

    void genPseudoLegalMoves(std::vector<Move>& out) const{
        out.clear();
        bool stmW = whiteToMove;

        auto add = [&](int from,int to,Piece promo=EMPTY){
            Move m; m.from=from; m.to=to; m.promo=promo; m.captured=b[to];
            out.push_back(m);
        };

        for(int sq=0;sq<64;sq++){
            Piece p=b[sq];
            if(p==EMPTY) continue;
            if(stmW && !isWhite(p)) continue;
            if(!stmW && !isBlack(p)) continue;

            int f=fileOf(sq), r=rankOf(sq);

            // Pawn
            if(p==WP || p==BP){
                int dir = (p==WP)? +1 : -1;
                int r2 = r + dir;
                int one = sq + dir*8;

                if(r2>=0 && r2<8 && onBoard(one) && b[one]==EMPTY){
                    if((p==WP && r2==7) || (p==BP && r2==0)){
                        add(sq, one, (p==WP)?WQ:BQ);
                        add(sq, one, (p==WP)?WR:BR);
                        add(sq, one, (p==WP)?WB:BB);
                        add(sq, one, (p==WP)?WN:BN);
                    }else add(sq, one);

                    // double push
                    if((p==WP && r==1) || (p==BP && r==6)){
                        int two = sq + dir*16;
                        if(onBoard(two) && b[two]==EMPTY) add(sq, two);
                    }
                }

                // captures + en passant
                for(int df: {-1,+1}){
                    int nf = f + df;
                    if(nf<0||nf>=8) continue;
                    int cap = sq + dir*8 + df;
                    if(!onBoard(cap)) continue;

                    // normal capture
                    if(b[cap]!=EMPTY && isWhite(p)!=isWhite(b[cap])){
                        if((p==WP && r2==7) || (p==BP && r2==0)){
                            add(sq, cap, (p==WP)?WQ:BQ);
                            add(sq, cap, (p==WP)?WR:BR);
                            add(sq, cap, (p==WP)?WB:BB);
                            add(sq, cap, (p==WP)?WN:BN);
                        }else add(sq, cap);
                    }

                    // en passant capture to epSq
                    if(cap==epSq){
                        // target is empty normally
                        Move m; m.from=sq; m.to=cap; m.promo=EMPTY; m.captured=EMPTY;
                        out.push_back(m);
                    }
                }
                continue;
            }

            // Knight
            if(p==WN || p==BN){
                static const int d[8]={+17,+15,+10,+6,-6,-10,-15,-17};
                for(int dv:d){
                    int to=sq+dv;
                    if(!onBoard(to)) continue;
                    int tf=fileOf(to), tr=rankOf(to);
                    if(std::max(std::abs(tf-f), std::abs(tr-r))!=2) continue;
                    if(b[to]==EMPTY || isWhite(b[to])!=isWhite(p)) add(sq,to);
                }
                continue;
            }

            auto slide = [&](const std::vector<int>& dirs){
                for(int dv:dirs){
                    int to=sq;
                    while(true){
                        int prev=to;
                        to+=dv;
                        if(!onBoard(to)) break;
                        int pf=fileOf(prev), tf=fileOf(to);

                        // 防止在水平或斜線方向「穿牆」到下一列/欄
                        // 水平 (±1)：檔案必須剛好差 1
                        if((dv==1 || dv==-1) && std::abs(tf - pf) != 1) break;
                        // 斜線 (±7, ±9)：檔案也必須剛好差 1
                        if((dv==7 || dv==-7 || dv==9 || dv==-9) && std::abs(tf - pf) != 1) break;

                        if(b[to]==EMPTY) add(sq,to);
                        else{
                            if(isWhite(b[to])!=isWhite(p)) add(sq,to);
                            break;
                        }
                    }
                }
            };

            if(p==WB || p==BB){ slide({+9,+7,-7,-9}); continue; }
            if(p==WR || p==BR){ slide({+8,-8,+1,-1}); continue; }
            if(p==WQ || p==BQ){ slide({+8,-8,+1,-1,+9,+7,-7,-9}); continue; }

            // King + castling
            if(p==WK || p==BK){
                static const int kd[8]={+8,-8,+1,-1,+9,+7,-7,-9};
                for(int dv:kd){
                    int to=sq+dv;
                    if(!onBoard(to)) continue;
                    int tf=fileOf(to);
                    if(std::abs(tf-f)>1) continue;
                    if(b[to]==EMPTY || isWhite(b[to])!=isWhite(p)) add(sq,to);
                }

                // castling generation (must be legal: squares empty + not in check + pass squares not attacked)
                if(p==WK && sq==4){
                    bool inCheck = squareAttacked(4, false);
                    // O-O
                    if((castle&1) && !inCheck && b[5]==EMPTY && b[6]==EMPTY
                       && !squareAttacked(5,false) && !squareAttacked(6,false)){
                        add(4,6);
                    }
                    // O-O-O
                    if((castle&2) && !inCheck && b[3]==EMPTY && b[2]==EMPTY && b[1]==EMPTY
                       && !squareAttacked(3,false) && !squareAttacked(2,false)){
                        add(4,2);
                    }
                }
                if(p==BK && sq==60){
                    bool inCheck = squareAttacked(60, true);
                    // O-O
                    if((castle&4) && !inCheck && b[61]==EMPTY && b[62]==EMPTY
                       && !squareAttacked(61,true) && !squareAttacked(62,true)){
                        add(60,62);
                    }
                    // O-O-O
                    if((castle&8) && !inCheck && b[59]==EMPTY && b[58]==EMPTY && b[57]==EMPTY
                       && !squareAttacked(59,true) && !squareAttacked(58,true)){
                        add(60,58);
                    }
                }
                continue;
            }
        }
    }

    void genLegalMoves(std::vector<Move>& out) {
        std::vector<Move> pseudo;
        genPseudoLegalMoves(pseudo);

        out.clear();
        out.reserve(pseudo.size());

        for(const auto& m : pseudo){
            Position p2 = *this;
            Undo u;
            p2.makeMove(m,u);

            // 走完後，輪到對方走，所以檢查「剛剛走的人」是否把自己王送進將軍
            bool movedWasWhite = !p2.whiteToMove;
            if(!p2.isInCheck(movedWasWhite)){
                out.push_back(m);
            }
        }
    }
};

struct Engine {
    Weights w;

    int eval(const Position& pos) const{
        double score=0;
        for(int sq=0;sq<64;sq++){
            Piece p=pos.b[sq];
            if(p==EMPTY) continue;

            auto idxPiece=[&](Piece x)->int{
                switch(x){
                    case WP: case BP: return 0;
                    case WN: case BN: return 1;
                    case WB: case BB: return 2;
                    case WR: case BR: return 3;
                    case WQ: case BQ: return 4;
                    case WK: case BK: return 5;
                    default: return 5;
                }
            };

            int id=idxPiece(p);
            double s = w.material[id];
            if(p==WP) s += w.pstPawn[sq];
            if(p==BP) s += w.pstPawn[63-sq];
            if(p==WN) s += w.pstKnight[sq];
            if(p==BN) s += w.pstKnight[63-sq];

            if(isWhite(p)) score += s;
            else score -= s;
        }
        return (int)std::llround(score);
    }

    int alphabeta(Position& pos, int depth, int alpha, int beta){
        if(depth<=0) return eval(pos) * (pos.whiteToMove ? 1 : -1);

        std::vector<Move> moves;
        pos.genLegalMoves(moves);
        if(moves.empty()) return 0;

        std::stable_sort(moves.begin(), moves.end(),
            [&](const Move& a,const Move& b){ return (a.captured!=EMPTY) > (b.captured!=EMPTY); });

        for(const auto& m : moves){
            Undo u;
            pos.makeMove(m,u);
            int val = -alphabeta(pos, depth-1, -beta, -alpha);
            pos.unmakeMove(m,u);
            if(val>=beta) return beta;
            if(val>alpha) alpha=val;
        }
        return alpha;
    }

    Move bestMove(const Position& pos, int depth, double epsilon=0.0, std::mt19937* rng=nullptr){
        Position p = pos;

        std::vector<Move> moves;
        p.genLegalMoves(moves);
        if(moves.empty()) return Move{};

        if(rng && epsilon>0.0){
            std::uniform_real_distribution<double> U(0.0,1.0);
            if(U(*rng)<epsilon){
                std::uniform_int_distribution<int> I(0,(int)moves.size()-1);
                return moves[I(*rng)];
            }
        }

        int bestScore=-1000000000;
        Move best=moves[0];

        for(const auto& m : moves){
            Position p2 = pos;
            Undo u;
            p2.makeMove(m, u);

            // 用 alphabeta 看 depth-1
            int sc = -alphabeta(p2, depth - 1, -1000000000, 1000000000);

            p2.unmakeMove(m, u);

            if(sc>bestScore){
                bestScore=sc;
                best=m;
            }
        }
        return best;
    }
};
