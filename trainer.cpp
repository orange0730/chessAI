#include "engine_real.hpp"
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <string>

// =========================
// Game / Match
// =========================

// 回傳：+1 白勝，0 和局，-1 黑勝
static int playGame(Engine white, Engine black, int depth, int maxPlies, std::mt19937& rng){
  Position pos;
  pos.setStartPos();

  const int RANDOM_OPENING_PLIES = 4; // 破對稱
  double eps = 0.15;                  // 探索

  for(int plies=0; plies<maxPlies; plies++){
    std::vector<Move> moves;
    pos.genLegalMoves(moves);

    if(moves.empty()){
      // 輪到走的人無子可走 -> 輸（簡化）
      return pos.whiteToMove ? -1 : +1;
    }

    Move m;
    if(plies < RANDOM_OPENING_PLIES){
      std::uniform_int_distribution<int> I(0, (int)moves.size()-1);
      m = moves[I(rng)];
    }else{
      Engine& side = pos.whiteToMove ? white : black;
      m = side.bestMove(pos, depth, eps, &rng);
    }

    Undo u;
    pos.makeMove(m, u);

    // 提早裁決：eval 差距大就判勝負（加速）
    int sc = white.eval(pos); // 白方視角
    if (sc > 200) return +1;
    if (sc < -200) return -1;


    eps *= 0.997;
  }

  // 走滿：用 eval 裁決
  int sc = white.eval(pos);
    if (sc > 30) return +1;
    if (sc < -30) return -1;


  return 0;
}

// wA vs wB 多盤，回傳 A 的平均得分（勝=1 和=0.5 負=0）
static double matchScore(const Weights& wA, const Weights& wB,
                         int games, int depth, std::mt19937& rng){
  double sum = 0.0;

  for(int i=0; i<games; i++){
    Engine A; A.w = wA;
    Engine B; B.w = wB;

    bool AisWhite = (i % 2 == 0);

    int resultWhite = AisWhite
      ? playGame(A, B, depth, 220, rng)
      : playGame(B, A, depth, 220, rng);

    double ptsA = 0.5;
    if(resultWhite == +1) ptsA = AisWhite ? 1.0 : 0.0;
    else if(resultWhite == -1) ptsA = AisWhite ? 0.0 : 1.0;

    sum += ptsA;
  }

  return sum / games;
}

// =========================
// ParamView
// =========================
struct ParamView {
  static constexpr int N = 6 + 64 + 64;

  static std::vector<double> flatten(const Weights& w){
    std::vector<double> x; x.reserve(N);
    for(int i=0;i<6;i++)   x.push_back((double)w.material[i]);
    for(int i=0;i<64;i++)  x.push_back((double)w.pstPawn[i]);
    for(int i=0;i<64;i++)  x.push_back((double)w.pstKnight[i]);
    return x;
  }

  static Weights unflatten(const std::vector<double>& x, const Weights& base){
    Weights w = base;
    int idx=0;

    for(int i=0;i<6;i++)   w.material[i]  = (int)llround(x[idx++]);
    for(int i=0;i<64;i++)  w.pstPawn[i]   = (int)llround(x[idx++]);
    for(int i=0;i<64;i++)  w.pstKnight[i] = (int)llround(x[idx++]);

    auto clampd = [&](double& v, double lo, double hi){
        if(v < lo) v = lo;
        if(v > hi) v = hi;
    };

    // material clamp
    clampd(w.material[0], 60, 200);
    clampd(w.material[1], 200, 500);
    clampd(w.material[2], 200, 500);
    clampd(w.material[3], 300, 800);
    clampd(w.material[4], 600, 1500);
    w.material[5] = 0;

    // pst clamp
    for(int i=0;i<64;i++){
      clampd(w.pstPawn[i],   -80, 120);
      clampd(w.pstKnight[i], -120, 120);
    }
    return w;
  }
};

// =========================
// Utility: save checkpoints
// =========================
static void saveCheckpoint(const std::string& path, const std::vector<double>& x){
  std::ofstream out(path, std::ios::binary);
  if(!out) return;
  uint32_t n = (uint32_t)x.size();
  out.write((const char*)&n, sizeof(n));
  out.write((const char*)x.data(), sizeof(double)*x.size());
}

static bool loadCheckpoint(const std::string& path, std::vector<double>& x){
  std::ifstream in(path, std::ios::binary);
  if(!in) return false;
  uint32_t n=0;
  in.read((char*)&n, sizeof(n));
  if(!in || n==0) return false;
  x.assign(n, 0.0);
  in.read((char*)x.data(), sizeof(double)*n);
  return (bool)in;
}

static std::pair<double,double> minmaxArr(const double* a, int n){
    double mn = a[0], mx = a[0];
    for(int i=1;i<n;i++){
        mn = std::min(mn, a[i]);
        mx = std::max(mx, a[i]);
    }
    return {mn, mx};
}


// ========================='

// Main
// =========================
int main(int argc, char** argv){

  if(argc >= 2 && std::string(argv[1]) == "export") {
    std::vector<double> x;
    if(!loadCheckpoint("checkpoint.bin", x)){
      std::cerr << "Cannot load checkpoint.bin\n";
      return 1;
    }

    Weights base = Weights::defaultWeights();
    base.load("weights.txt"); // 用來當作 base 結構（主要是大小一致）

    Weights cur = ParamView::unflatten(x, base);
    cur.save("weights_ckpt.txt");

    std::cout << "[OK] exported weights_ckpt.txt from checkpoint.bin\n";
    std::cout << "material[0]=" << cur.material[0]
              << " pawnPST0=" << cur.pstPawn[0]
              << " knightPST0=" << cur.pstKnight[0] << "\n";
    return 0;
  }
  // 用法：
  // trainer.exe iterations games depth verify_games
  // 例：trainer.exe 12000 25 2 200

  int iterations    = 20000;
  int gamesPerEval  = 200;
  int depth         = 3;
  int verifyGames   = 400;   // ← 預設值（沒給參數時用）

  if(argc >= 2) iterations   = std::atoi(argv[1]);
  if(argc >= 3) gamesPerEval = std::atoi(argv[2]);
  if(argc >= 4) depth        = std::atoi(argv[3]);
  if(argc >= 5) verifyGames  = std::atoi(argv[4]);

  // 長跑額外參數（可直接改這裡）
  const int PRINT_EVERY       = 20;   // 每 20 iter 印一次
  const int CHECKPOINT_EVERY  = 50;   // 每 50 iter 存一次 current checkpoint
  
  const int MAX_PLIES         = 220;

  unsigned seed = (unsigned)std::time(nullptr);
  std::mt19937 rng(seed);

  // base：從 weights.txt 起跑（best 也是以這個作為對手）
  Weights base = Weights::defaultWeights();
  base.load("weights.txt");

  std::vector<double> x = ParamView::flatten(base);

  // 若有 checkpoint，就從 checkpoint 繼續（避免中斷重來）
  {
    std::vector<double> chk;
    if(loadCheckpoint("checkpoint.bin", chk) && chk.size() == x.size()){
      x = chk;
      std::cout << "[Resume] Loaded checkpoint.bin\n";
    }
  }

  std::vector<double> bestX = x;
  double bestScore = 0.5;

  // SPSA 參數（長跑更建議這組）
  // a: 步長、c: 擾動尺度（PST 要夠大才會跨過 rounding）
  
  double a = 8.0;      // 步長放大
  double c = 10.0;     // 擾動尺度放大（讓 round 後真的有變）
  double A = 200.0;    // 前期更穩，不會亂飄
  double alpha = 0.602;
  double gamma = 0.101;

  std::uniform_int_distribution<int> pm(0,1);

  std::cout << "SPSA training start\n";
  std::cout << "iterations="<<iterations
          << " games="<<gamesPerEval
          << " depth="<<depth
          << " verifyGames="<<verifyGames
          << "\n";

  std::cout << "params: a="<<a<<" c="<<c<<" A="<<A<<" alpha="<<alpha<<" gamma="<<gamma<<"\n";
  std::cout << "verifyGames="<<verifyGames<<" printEvery="<<PRINT_EVERY<<" checkpointEvery="<<CHECKPOINT_EVERY<<"\n";

  for(int k=0;k<iterations;k++){
    double ak = a / std::pow(A + k + 1.0, alpha);
    double ck = c / std::pow(k + 1.0, gamma);

    std::vector<double> delta(x.size());
    for(size_t i=0;i<x.size();i++){
      delta[i] = (pm(rng)==0) ? -1.0 : +1.0;
    }

    std::vector<double> xPlus=x, xMinus=x;
    for(size_t i=0;i<x.size();i++){
      xPlus[i]  = x[i] + ck*delta[i];
      xMinus[i] = x[i] - ck*delta[i];
    }

    Weights wPlus  = ParamView::unflatten(xPlus,  base);
    Weights wMinus = ParamView::unflatten(xMinus, base);

    // 差分：兩邊都算（比較穩）
    double sPlus  = matchScore(wPlus,  wMinus, gamesPerEval, depth, rng);
    double sMinus = matchScore(wMinus, wPlus,  gamesPerEval, depth, rng);
    double yDiff = sPlus - sMinus;

    for(size_t i=0;i<x.size();i++){
      double ghat = (yDiff / (2.0*ck)) * delta[i];
      x[i] = x[i] + ak * ghat;
    }

    Weights current = ParamView::unflatten(x, base);
    double scoreVsBase = matchScore(current, base, gamesPerEval, depth, rng);

    if((k+1) % PRINT_EVERY == 0 || k==0){
      std::cout << "iter " << (k+1)
                << " sPlus="<<std::fixed<<std::setprecision(3)<<sPlus
                << " sMinus="<<sMinus
                << " yDiff="<<yDiff
                << " scoreVsBase="<<scoreVsBase
                << " ak="<<std::setprecision(4)<<ak
                << " ck="<<ck
                << " x0="<<std::setprecision(3)<<x[0]
                << "\n";
    }

    // checkpoint: 讓你睡覺也不怕當機
    if((k+1) % CHECKPOINT_EVERY == 0){
      saveCheckpoint("checkpoint.bin", x);
    }

    // best：先 verify 再存，避免噪音亂存
    if(scoreVsBase > bestScore){
      double verify = matchScore(current, base, verifyGames, depth, rng);
      if(verify > bestScore){
        bestScore = verify;
        bestX = x;

        Weights bestW = ParamView::unflatten(bestX, base);
        bestW.save("weights.txt");

        auto [pMn,pMx] = minmaxArr(bestW.pstPawn, 64);
        auto [nMn,nMx] = minmaxArr(bestW.pstKnight, 64);

        std::cout << "  >> VERIFIED new best saved (bestScore="<<std::fixed<<std::setprecision(3)<<bestScore<<")\n";
        std::cout << "     material=["<<bestW.material[0]<<","<<bestW.material[1]<<","<<bestW.material[2]
                  <<","<<bestW.material[3]<<","<<bestW.material[4]<<"] "
                  << "pstPawn(min,max)=("<<pMn<<","<<pMx<<") "
                  << "pstKnight(min,max)=("<<nMn<<","<<nMx<<")\n";
      }
    }
  }

  // 收尾：存一次 checkpoint
  saveCheckpoint("checkpoint.bin", x);

  std::cout << "Training done. Best scoreVsBase="<<std::fixed<<std::setprecision(3)<<bestScore<<"\n";
  return 0;
}
