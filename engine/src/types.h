#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>

namespace chess {

// ========================
// Piece and Side enums
// ========================

enum Piece {
    EMPTY = 0,
    WP, WN, WB, WR, WQ, WK,
    BP, BN, BB, BR, BQ, BK
};

enum Side { WHITE = 0, BLACK = 1 };

// ========================
// Constants
// ========================

constexpr int INF = 1000000;
constexpr int MATE_SCORE = 900000;

// ========================
// Utility functions
// ========================

template <typename T>
inline T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline bool is_white(Piece p) { return p >= WP && p <= WK; }
inline bool is_black(Piece p) { return p >= BP && p <= BK; }

inline int piece_type(Piece p) {
    if (p == EMPTY) return 0;
    if (p <= WK) return p;
    return p - 6;
}

inline int file_of(int sq) { return sq & 7; }
inline int rank_of(int sq) { return sq >> 3; }
inline int make_sq(int file, int rank) { return rank * 8 + file; }
inline int mirror_sq(int sq) { return make_sq(file_of(sq), 7 - rank_of(sq)); }

inline Side opposite(Side s) { return s == WHITE ? BLACK : WHITE; }

inline std::string sq_to_str(int sq) {
    std::string s;
    s.push_back(char('a' + file_of(sq)));
    s.push_back(char('1' + rank_of(sq)));
    return s;
}

inline int str_to_sq(const std::string &s) {
    if (s.size() < 2) return -1;
    int f = s[0] - 'a';
    int r = s[1] - '1';
    if (f < 0 || f > 7 || r < 0 || r > 7) return -1;
    return make_sq(f, r);
}

inline bool is_opponent(int p, int target) {
    if (target == EMPTY) return false;
    if (target == WK || target == BK) return false;
    if (p <= WK) return target >= BP;
    return target <= WK;
}

inline int piece_value(int p) {
    switch (piece_type((Piece)p)) {
        case WP: return 100;
        case WN: return 320;
        case WB: return 330;
        case WR: return 500;
        case WQ: return 900;
        case WK: return 20000;
        default: return 0;
    }
}

inline char piece_to_char(Piece p) {
    switch (p) {
        case WP: return 'P'; case WN: return 'N'; case WB: return 'B';
        case WR: return 'R'; case WQ: return 'Q'; case WK: return 'K';
        case BP: return 'p'; case BN: return 'n'; case BB: return 'b';
        case BR: return 'r'; case BQ: return 'q'; case BK: return 'k';
        default: return '.';
    }
}

inline int char_to_piece(char c) {
    switch (c) {
        case 'P': return WP; case 'N': return WN; case 'B': return WB;
        case 'R': return WR; case 'Q': return WQ; case 'K': return WK;
        case 'p': return BP; case 'n': return BN; case 'b': return BB;
        case 'r': return BR; case 'q': return BQ; case 'k': return BK;
        default: return EMPTY;
    }
}

inline int promo_from_char(char c, Side side) {
    switch (c) {
        case 'q': return side == WHITE ? WQ : BQ;
        case 'r': return side == WHITE ? WR : BR;
        case 'b': return side == WHITE ? WB : BB;
        case 'n': return side == WHITE ? WN : BN;
        default: return EMPTY;
    }
}

// ========================
// Data Structures
// ========================

struct Move {
    int from = -1;
    int to = -1;
    int promotion = EMPTY;
    bool is_en_passant = false;
    bool is_castle = false;
    int captured = EMPTY;
    int moved = EMPTY;
};

struct Undo {
    int castling = 0;
    int ep_square = -1;
    int halfmove = 0;
    int fullmove = 1;
    int captured = EMPTY;
    Move last_move_white{};
    Move last_move_black{};
};

struct Position {
    std::array<int, 64> board{};
    Side side_to_move = WHITE;
    int castling_rights = 0; // 1 WK, 2 WQ, 4 BK, 8 BQ
    int ep_square = -1;
    int halfmove_clock = 0;
    int fullmove_number = 1;
    uint64_t hash = 0;

    Position() { board.fill(EMPTY); }
};

struct HashKeys {
    std::array<std::array<uint64_t, 13>, 64> piece{};
    uint64_t side = 0;
    std::array<uint64_t, 16> castling{};
    std::array<uint64_t, 9> ep{}; // file or 8 for no ep

    HashKeys() {
        std::mt19937_64 rng(20260129);
        auto rand64 = [&]() { return rng(); };
        for (int sq = 0; sq < 64; ++sq) {
            for (int p = 0; p < 13; ++p) {
                piece[sq][p] = rand64();
            }
        }
        side = rand64();
        for (auto &v : castling) v = rand64();
        for (auto &v : ep) v = rand64();
    }
};

enum TTFlag : uint8_t { TT_EXACT = 0, TT_ALPHA = 1, TT_BETA = 2 };

static constexpr int TT_SIZE = 1 << 20; // ~1M entries
static constexpr int TT_MASK = TT_SIZE - 1;

struct TTEntry {
    uint64_t hash = 0;
    int depth = 0;
    int score = 0;
    TTFlag flag = TT_EXACT;
    Move best;
};

// ========================
// Functions requiring Move struct
// ========================

inline std::string move_to_uci(const Move &m) {
    std::string s = sq_to_str(m.from) + sq_to_str(m.to);
    if (m.promotion != EMPTY) {
        char pc = std::tolower(piece_to_char((Piece)m.promotion));
        s.push_back(pc);
    }
    return s;
}

} // namespace chess
