#pragma once
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>

// --- Minimal types to satisfy compile + allow dummy play ---
// 這份是「先讓專案跑起來」的最小引擎骨架；之後再換回完整引擎。

enum Piece {
  EMPTY=0,
  WP,WN,WB,WR,WQ,WK,
  BP,BN,BB,BR,BQ,BK
};

struct Move{
  int from=0, to=0;
  Piece promo=EMPTY;
};

struct Undo{};

struct Weights{
  int material[6]{100,320,330,500,900,0};
  int pstPawn[64]{0};
  int pstKnight[64]{0};

  static Weights defaultWeights(){ return Weights(); }

  bool load(const std::string& path){
    std::ifstream in(path);
    if(!in) return false;
    for(int i=0;i<6;i++) in >> material[i];
    for(int i=0;i<64;i++) in >> pstPawn[i];
    for(int i=0;i<64;i++) in >> pstKnight[i];
    return true;
  }

  bool save(const std::string& path) const{
    std::ofstream out(path);
    if(!out) return false;
    for(int i=0;i<6;i++){
      if(i) out << ' ';
      out << material[i];
    }
    out << "\n";
    for(int i=0;i<64;i++){
      if(i) out << ' ';
      out << pstPawn[i];
    }
    out << "\n";
    for(int i=0;i<64;i++){
      if(i) out << ' ';
      out << pstKnight[i];
    }
    out << "\n";
    return true;
  }
};



struct Position{
  bool whiteToMove=true;
  int halfmoveClock=0;

  void setStartPos(){
    whiteToMove=true;
    halfmoveClock=0;
  }

  void setFEN(const std::string&){
    setStartPos();
  }

  bool isInCheck(bool) const { return false; }

  void genLegalMoves(std::vector<Move>& out) const {
    // 先用假走法讓流程能跑（不是真棋）
    out.clear();
    // 生成幾個無意義 move（避免空 move list）
    for(int i=0;i<8;i++){
      Move m; m.from=i; m.to=i+8;
      out.push_back(m);
    }
  }

  void makeMove(const Move&, Undo&){
    whiteToMove = !whiteToMove;
    halfmoveClock++;
  }
};

struct Engine{
  Weights w;

  Move bestMove(const Position& pos, int /*depth*/, double epsilon=0.0, std::mt19937* rng=nullptr){
    std::vector<Move> moves;
    pos.genLegalMoves(moves);
    if(moves.empty()) return Move{};
    if(rng && epsilon>0.0){
      std::uniform_real_distribution<double> U(0.0,1.0);
      if(U(*rng) < epsilon){
        std::uniform_int_distribution<int> I(0, (int)moves.size()-1);
        return moves[I(*rng)];
      }
    }
    return moves[0];
  }

  
};
