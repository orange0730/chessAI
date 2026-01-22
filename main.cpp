#include "engine_real.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <string>
#include <cctype>
#include <random>
#include <cstdlib>
#include <algorithm>

// ============================
// 小工具：座標轉換
// ============================
static int fileCharToX(char f){ return f - 'a'; }
static int rankCharToY(char r){ return r - '1'; }   // '1'->0, '8'->7
static int xyToSq(int x,int y){ return y*8 + x; }   // a1=0

static std::string sqToStr(int sq){
    int x = sq % 8;
    int y = sq / 8;
    std::string s;
    s += char('a' + x);
    s += char('1' + y);   // y=0 -> '1'
    return s;
}



static char normPromoChar(char c){
    c = (char)std::tolower((unsigned char)c);
    if(c=='q'||c=='r'||c=='b'||c=='n') return c;
    return 0;
}

// ============================
// UCI move 解析 / 輸出（不靠外部函式）
// ============================
static bool parseUciMoveLocal(Position& pos, const std::string& uci, Move& out){
    if(uci.size() < 4) return false;

    int fx = fileCharToX(uci[0]);
    int fy = rankCharToY(uci[1]);
    int tx = fileCharToX(uci[2]);
    int ty = rankCharToY(uci[3]);
    if(fx<0||fx>=8||fy<0||fy>=8||tx<0||tx>=8||ty<0||ty>=8) return false;

    int from = xyToSq(fx, fy);
    int to   = xyToSq(tx, ty);

    char promo = 0;
    if(uci.size() >= 5) promo = normPromoChar(uci[4]);

    std::vector<Move> moves;
    pos.genLegalMoves(moves);

    for(const auto& m : moves){
        if(m.from == from && m.to == to){
            if(promo == 0){
                out = m;
                return true;
            }else{
                if(std::tolower((unsigned char)m.promo) == promo){
                    out = m;
                    return true;
                }
            }
        }
    }
    return false;
}

static char promoToChar(Piece p){
    switch(p){
        case WQ: case BQ: return 'q';
        case WR: case BR: return 'r';
        case WB: case BB: return 'b';
        case WN: case BN: return 'n';
        default: return 0;
    }
}

static std::string moveToUciLocal(const Move& m){
    std::string s = sqToStr(m.from) + sqToStr(m.to);

    // 只有真的升變步才加第 5 碼
    char pc = promoToChar(m.promo);
    if(pc) s += pc;
    int toRank = m.to / 8;
    if(pc){
        // 必須是兵升變：白到第 8 排 or 黑到第 1 排
        if(!(toRank == 7 || toRank == 0)){
            pc = 0; // 不合理就不輸出 promo
        }
    }

    return s;
}


static std::string moveToUci(const Move& m){
    return moveToUciLocal(m);
}

// ============================
// Bench：自動對戰測試
// ============================
static int playGameBench(Engine& white, Engine& black, int depth, int maxPlies, std::mt19937& rng) {
    Position pos;
    pos.setStartPos();

    const int RANDOM_OPENING_PLIES = 8;
    double eps = 0.10;

    for (int plies = 0; plies < maxPlies; plies++) {
        std::vector<Move> moves;
        pos.genLegalMoves(moves);

        if (moves.empty()) {
            return pos.whiteToMove ? -1 : +1;
        }

        Move m;
        if (plies < RANDOM_OPENING_PLIES) {
            std::uniform_int_distribution<int> I(0, (int)moves.size() - 1);
            m = moves[I(rng)];
        } else {
            Engine& side = pos.whiteToMove ? white : black;
            m = side.bestMove(pos, depth, eps, &rng);
        }

        Undo u;
        pos.makeMove(m, u);

        int sc = white.eval(pos);

        if (sc > 600) return +1;
        if (sc < -600) return -1;

        eps *= 0.997;
    }

    int sc = white.eval(pos);
    if (sc > 80) return +1;
    if (sc < -80) return -1;
    return 0;
}

static void runBench(int games, int depth) {
    Engine A, B;

    A.w = Weights::defaultWeights();
    A.w.load("weights.txt");           // 訓練後權重

    B.w = Weights::defaultWeights();   // baseline

    std::cout << "[DBG] A material0=" << A.w.material[0]
              << " B material0=" << B.w.material[0] << "\n";
    std::cout << "[DBG] A pawnPST0=" << A.w.pstPawn[0]
              << " B pawnPST0=" << B.w.pstPawn[0] << "\n";
    std::cout.flush();

    int win = 0, draw = 0, loss = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    std::mt19937 rng((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());

    for (int i = 0; i < games; i++) {
        bool AisWhite = (i % 2 == 0);

        int res = AisWhite
            ? playGameBench(A, B, depth, 220, rng)
            : playGameBench(B, A, depth, 220, rng);

        if (res == 0) draw++;
        else if ((res == +1 && AisWhite) || (res == -1 && !AisWhite)) win++;
        else loss++;

        if ((i + 1) % 20 == 0) {
            double score = (win + 0.5 * draw) / (i + 1);
            std::cout << "[bench] " << (i + 1) << "/" << games
                      << " W/D/L=" << win << "/" << draw << "/" << loss
                      << " score=" << std::fixed << std::setprecision(3)
                      << score << "\n";
            std::cout.flush();
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    double score = (win + 0.5 * draw) / games;

    std::cout << "\n=== BENCH DONE ===\n";
    std::cout << "Games : " << games << "\n";
    std::cout << "Depth : " << depth << "\n";
    std::cout << "W/D/L : " << win << "/" << draw << "/" << loss << "\n";
    std::cout << "Score : " << std::fixed << std::setprecision(4) << score << "\n";
    std::cout << "Time  : " << sec << " sec\n";
    std::cout.flush();
}

// ============================
// UCI 模式
// ============================
static void runUCI() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Position pos;
    pos.setStartPos();

    Engine engine;
    engine.w = Weights::defaultWeights();
    engine.w.load("weights.txt");

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "uci") {
            std::cout << "id name MinimalCPPChessAI\n";
            std::cout << "id author you\n";
            std::cout << "uciok\n" << std::flush;
        }
        else if (line == "isready") {
            std::cout << "readyok\n" << std::flush;
        }
        else if (line == "ucinewgame") {
            pos.setStartPos();
        }
        else if (line.rfind("position", 0) == 0) {
            std::stringstream ss(line);
            std::string tok;
            ss >> tok; // position
            ss >> tok;

            if (tok == "startpos") {
                pos.setStartPos();
            }
            else if (tok == "fen") {
                // 讀取標準 FEN 六個欄位後丟給 Position::setFEN
                std::string board, active, castlingStr, epStr, halfStr, fullStr;
                if (ss >> board >> active >> castlingStr >> epStr >> halfStr >> fullStr) {
                    std::string fen = board + " " + active + " " + castlingStr + " " + epStr + " " + halfStr + " " + fullStr;
                    pos.setFEN(fen);
                } else {
                    std::cout << "info string [WARN] bad fen, falling back to startpos\n" << std::flush;
                    pos.setStartPos();
                }
            }


            if (ss >> tok && tok == "moves") {
                while (ss >> tok) {
                    Move m;
                    if (parseUciMoveLocal(pos, tok, m)) {
                        Undo u;
                        pos.makeMove(m, u);
                    } else {
                        std::cout << "info string [ERR] cannot parse move " << tok << "\n" << std::flush;
                        break;
                    }
                }
            }

        }
        else if (line.rfind("go", 0) == 0) {
            int depth = 4;
            std::stringstream ss(line);
            std::string tok;
            ss >> tok;
            while (ss >> tok) {
                if (tok == "depth") ss >> depth;
            }

            Move bm = engine.bestMove(pos, depth, 0.0, nullptr);

            // ===== 保證 bm 一定在合法棋清單內 =====
            std::vector<Move> legal;
            pos.genLegalMoves(legal);

            auto sameMove = [&](const Move& a, const Move& b){
                return a.from == b.from && a.to == b.to && a.promo == b.promo;
            };

            bool ok = false;
            for(const auto& m : legal){
                if(sameMove(m, bm)){ ok = true; break; }
            }

            if(!ok){
                if(!legal.empty()) bm = legal[0];
            }

            std::string uci = moveToUci(bm);
            if(uci.size() < 4) uci = "0000";
            std::cout << "bestmove " << uci << "\n" << std::flush;
        }
        else if (line == "quit") {
            break;
        }
    }
}

// ============================
// main：模式分流
// ============================
int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "bench") {
        int games = (argc >= 3) ? std::atoi(argv[2]) : 200;
        int depth = (argc >= 4) ? std::atoi(argv[3]) : 4;
        runBench(games, depth);
        return 0;
    }

    runUCI();
    return 0;
}
