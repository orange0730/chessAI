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
// 注意：這裡用「寬鬆解析」
// - 外部（cutechess/GUI）送來的 moves 本來就應該是合法棋譜
// - 若我們用自己的 genLegalMoves 去驗證，任何規則 bug 都會造成同步失敗，後面就整盤飄掉
// 需要嚴格合法性時，請在我們自己決策（go/bestmove）階段用 genLegalMoves 控制。
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

    Move m;
    m.from = from;
    m.to   = to;
    m.captured = pos.b[to];

    if(promo){
        // UCI promotion 字元只會出現在兵升變
        bool wtm = pos.whiteToMove;
        if(promo=='q') m.promo = wtm ? WQ : BQ;
        else if(promo=='r') m.promo = wtm ? WR : BR;
        else if(promo=='b') m.promo = wtm ? WB : BB;
        else if(promo=='n') m.promo = wtm ? WN : BN;
    }

    out = m;
    return true;
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
    if(pc){
        int toRank = m.to / 8;
        if(toRank == 7 || toRank == 0){
            s += pc;
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
            long long wtime=-1, btime=-1, winc=0, binc=0, movetime=-1;
            int movestogo = -1;

            std::stringstream ss(line);
            std::string tok;
            ss >> tok;
            while (ss >> tok) {
                if (tok == "depth") ss >> depth;
                else if (tok == "wtime") ss >> wtime;
                else if (tok == "btime") ss >> btime;
                else if (tok == "winc") ss >> winc;
                else if (tok == "binc") ss >> binc;
                else if (tok == "movetime") ss >> movetime;
                else if (tok == "movestogo") ss >> movestogo;
            }

            // --- time management ---
            engine.useTime = false;
            if (movetime > 0) {
                engine.useTime = true;
                engine.endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(movetime);
            } else if (wtime >= 0 && btime >= 0) {
                long long remain = pos.whiteToMove ? wtime : btime;
                long long inc    = pos.whiteToMove ? winc  : binc;

                // 估計剩餘步數：若 GUI 沒給 movestogo，用 25 當快棋保守值
                int mtg = (movestogo > 0) ? movestogo : 25;

                // 基本配額：剩餘時間 / mtg，再加上一點 increment
                long long budget = remain / mtg + (inc * 8) / 10;

                // 安全上下限（避免一次花光/也避免太小）
                long long minBudget = 50;
                long long maxBudget = std::max(200LL, remain / 2);
                if (budget < minBudget) budget = minBudget;
                if (budget > maxBudget) budget = maxBudget;

                engine.useTime = true;
                engine.endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);
            }

            // --- iterative deepening ---
            Move bm;
            int maxDepth = depth;
            if (engine.useTime && maxDepth < 64) maxDepth = 64; // 時間制下盡量往下搜，會在 timeUp() 自動停

            for (int d = 1; d <= maxDepth; d++) {
                Move cand = engine.bestMove(pos, d, 0.0, nullptr);
                // 若 timeUp，bestMove 可能提前 break；仍保留上一層結果
                if (!engine.useTime || !engine.timeUp()) {
                    bm = cand;
                }
                if (engine.useTime && engine.timeUp()) break;
            }

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
