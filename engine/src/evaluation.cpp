#include "evaluation.h"
#include "position.h"
#include "movegen.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace chess {

// ========================
// PST Tables (file-scope) — PeSTO-style, middlegame + endgame
// ========================

// Middlegame tables
static std::array<int, 64> pst_pawn_mg;
static std::array<int, 64> pst_knight_mg;
static std::array<int, 64> pst_bishop_mg;
static std::array<int, 64> pst_rook_mg;
static std::array<int, 64> pst_queen_mg;
static std::array<int, 64> pst_king_mg;

// Endgame tables
static std::array<int, 64> pst_pawn_eg;
static std::array<int, 64> pst_knight_eg;
static std::array<int, 64> pst_bishop_eg;
static std::array<int, 64> pst_rook_eg;
static std::array<int, 64> pst_queen_eg;
static std::array<int, 64> pst_king_eg;

// Backward-compat aliases
static std::array<int, 64> pst_pawn;
static std::array<int, 64> pst_knight;
static std::array<int, 64> pst_bishop;
static std::array<int, 64> pst_rook;
static std::array<int, 64> pst_queen;
static std::array<int, 64> pst_king;
static std::array<int, 64> pst_king_endgame;

void init_pst_tables() {
    // ========== MIDDLEGAME PSTs ==========
    // Pawn MG — encourage central pawns and advancement
    pst_pawn_mg = {
          0,   0,   0,   0,   0,   0,   0,   0,
         98, 134,  61,  95,  68, 126,  34, -11,
         -6,   7,  26,  31,  65,  56,  25, -20,
        -14,  13,   6,  21,  23,  12,  17, -23,
        -27,  -2,  -5,  12,  17,   6,  10, -25,
        -26,  -4,  -4, -10,   3,   3,  33, -12,
        -35,  -1, -20, -23, -15,  24,  38, -22,
          0,   0,   0,   0,   0,   0,   0,   0
    };
    // Knight MG
    pst_knight_mg = {
       -167, -89, -34, -49,  61, -97, -15,-107,
        -73, -41,  72,  36,  23,  62,   7, -17,
        -47,  60,  37,  65,  84, 129,  73,  44,
         -9,  17,  19,  53,  37,  69,  18,  22,
        -13,   4,  16,  13,  28,  19,  21,  -8,
        -23,  -9,  12,  10,  19,  17,  25, -16,
        -29, -53, -12,  -3,  -1,  18, -14, -19,
       -105, -21, -58, -33, -17, -28, -19, -23
    };
    // Bishop MG
    pst_bishop_mg = {
        -29,   4, -82, -37, -25, -42,   7,  -8,
        -26,  16, -18, -13,  30,  59,  18, -47,
        -16,  37,  43,  40,  35,  50,  37,  -2,
         -4,   5,  19,  50,  37,  37,   7,  -2,
         -6,  13,  13,  26,  34,  12,  10,   4,
          0,  15,  15,  15,  14,  27,  18,  10,
          4,  15,  16,   0,   7,  21,  33,   1,
        -33,  -3, -14, -21, -13, -12, -39, -21
    };
    // Rook MG
    pst_rook_mg = {
         32,  42,  32,  51,  63,   9,  31,  43,
         27,  32,  58,  62,  80,  67,  26,  44,
         -5,  19,  26,  36,  17,  45,  61,  16,
        -24, -11,   7,  26,  24,  35,  -8, -20,
        -36, -26, -12,  -1,   9,  -7,   6, -23,
        -45, -25, -16, -17,   3,   0,  -5, -33,
        -44, -16, -20,  -9,  -1,  11,  -6, -71,
        -19, -13,   1,  17,  16,   7, -37, -26
    };
    // Queen MG
    pst_queen_mg = {
        -28,   0,  29,  12,  59,  44,  43,  45,
        -24, -39,  -5,   1, -16,  57,  28,  54,
        -13, -17,   7,   8,  29,  56,  47,  57,
        -27, -27, -16, -16,  -1,  17,  -2,   1,
         -9, -26,  -9, -10,  -2,  -4,   3,  -3,
        -14,   2, -11,  -2,  -5,   2,  14,   5,
        -35,  -8,  11,   2,   8,  15,  -3,   1,
         -1, -18,  -9,  10, -15, -25, -31, -50
    };
    // King MG — stay castled, avoid center
    pst_king_mg = {
        -65,  23,  16, -15, -56, -34,   2,  13,
         29,  -1, -20,  -7,  -8,  -4, -38, -29,
         -9,  24,   2, -16, -20,   6,  22, -22,
        -17, -20, -12, -27, -30, -25, -14, -36,
        -49,  -1, -27, -39, -46, -44, -33, -51,
        -14, -14, -22, -46, -44, -30, -15, -27,
          1,   7,  -8, -64, -43, -16,   9,   8,
        -15,  36,  12, -54,   8, -28,  24,  14
    };

    // ========== ENDGAME PSTs ==========
    pst_pawn_eg = {
          0,   0,   0,   0,   0,   0,   0,   0,
        178, 173, 158, 134, 147, 132, 165, 187,
         94, 100,  85,  67,  56,  53,  82,  84,
         32,  24,  13,   5,  -2,   4,  17,  17,
         13,   9,  -3,  -7,  -7,  -8,   3,  -1,
          4,   7,  -6,   1,   0,  -5,  -1,  -8,
         13,   8,   8, -10,  -6,  -7,  -1,  -2,
          0,   0,   0,   0,   0,   0,   0,   0
    };
    pst_knight_eg = {
        -58, -38, -13, -28, -31, -27, -63, -99,
        -25,  -8, -25,  -2,  -9, -25, -24, -52,
        -24, -20,  10,   9,  -1,  -9, -19, -41,
        -17,   3,  22,  22,  22,  11,   8, -18,
        -18,  -6,  16,  25,  16,  17,   4, -18,
        -23,  -3,  -1,  15,  10,  -3, -20, -22,
        -42, -20, -10,  -5,  -2, -20, -23, -44,
        -29, -51, -23, -15, -22, -18, -50, -64
    };
    pst_bishop_eg = {
        -14, -21, -11,  -8,  -7,  -9, -17, -24,
         -8,  -4,   7, -12,  -3, -13,  -4, -14,
          2,  -8,   0,  -1,  -2,   6,   0,   4,
         -3,   9,  12,   9,  14,  10,   3,   2,
         -6,   3,  13,  19,   7,  10,  -3,  -9,
        -12,  -3,   8,  10,  13,   3,  -7, -15,
        -14, -18,  -7,  -1,   4,  -9, -15, -27,
        -23,  -9, -23,  -5,  -9, -16,  -5, -17
    };
    pst_rook_eg = {
         13,  10,  18,  15,  12,  12,   8,   5,
         11,  13,  13,  11,  -3,   3,   8,   3,
          7,   7,   7,   5,   4,  -3,  -5,  -3,
          4,   3,  13,   1,   2,   1,  -1,   2,
          3,   5,   8,   4,  -5,  -6,  -8, -11,
         -4,   0,  -5,  -1,  -7, -12,  -8, -16,
         -6,  -6,   0,   2,  -9,  -9, -11,  -3,
         -9,   2,   3,  -1,  -5, -13,   4, -20
    };
    pst_queen_eg = {
         -9,  22,  22,  27,  27,  19,  10,  20,
        -17,  20,  32,  41,  58,  25,  30,   0,
        -20,   6,   9,  49,  47,  35,  19,   9,
          3,  22,  24,  45,  57,  40,  57,  36,
        -18,  28,  19,  47,  31,  34,  39,  23,
        -16, -27,  15,   6,   9,  17,  10,   5,
        -22, -23, -30, -16, -16, -23, -36, -32,
        -33, -28, -22, -43,  -5, -32, -20, -41
    };
    pst_king_eg = {
        -74, -35, -18, -18, -11,  15,   4, -17,
        -12,  17,  14,  17,  17,  38,  23,  11,
         10,  17,  23,  15,  20,  45,  44,  13,
         -8,  22,  24,  27,  26,  33,  26,   3,
        -18,  -4,  21,  24,  27,  23,   9, -11,
        -19,  -3,  11,  21,  23,  16,   7,  -9,
        -27, -11,   4,  13,  14,   4,  -5, -17,
        -53, -34, -21, -11, -28, -14, -24, -43
    };

    // Backward-compat aliases (used by some old code paths)
    pst_pawn = pst_pawn_mg;
    pst_knight = pst_knight_mg;
    pst_bishop = pst_bishop_mg;
    pst_rook = pst_rook_mg;
    pst_queen = pst_queen_mg;
    pst_king = pst_king_mg;
    pst_king_endgame = pst_king_eg;
}

// ========================
// Threat & Tactical Evaluation
// ========================

static bool is_knight_fork_threat(const Board &board, int knight_sq, Side attacker) {
    int king_target = (attacker == WHITE) ? BK : WK;
    int queen_target = (attacker == WHITE) ? BQ : WQ;
    int rook_target = (attacker == WHITE) ? BR : WR;

    bool attacks_king = false;
    bool attacks_queen = false;
    bool attacks_rook = false;

    static const int knight_offsets[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
    int f = file_of(knight_sq);
    int r = rank_of(knight_sq);

    for (auto &o : knight_offsets) {
        int nf = f + o[0];
        int nr = r + o[1];
        if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
        int target_sq = make_sq(nf, nr);
        int p = board.pos.board[target_sq];
        if (p == king_target) attacks_king = true;
        if (p == queen_target) attacks_queen = true;
        if (p == rook_target) attacks_rook = true;
    }

    return attacks_king && (attacks_queen || attacks_rook);
}

static int count_knight_fork_threats(Board &board, Side attacker) {
    int forks = 0;
    int knight_piece = (attacker == WHITE) ? WN : BN;

    for (int sq = 0; sq < 64; ++sq) {
        if (board.pos.board[sq] == knight_piece) {
            static const int knight_offsets[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
            int f = file_of(sq);
            int r = rank_of(sq);

            for (auto &o : knight_offsets) {
                int nf = f + o[0];
                int nr = r + o[1];
                if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
                int target_sq = make_sq(nf, nr);
                int target = board.pos.board[target_sq];
                if (target == EMPTY ||
                    (attacker == WHITE && is_black((Piece)target)) ||
                    (attacker == BLACK && is_white((Piece)target))) {
                    int orig = board.pos.board[target_sq];
                    board.pos.board[sq] = EMPTY;
                    board.pos.board[target_sq] = knight_piece;
                    if (is_knight_fork_threat(board, target_sq, attacker)) {
                        forks++;
                    }
                    board.pos.board[target_sq] = orig;
                    board.pos.board[sq] = knight_piece;
                }
            }
        }
    }
    return forks;
}

static int evaluate_hanging_pieces(Board &board) {
    int score = 0;
    for (int sq = 0; sq < 64; ++sq) {
        int p = board.pos.board[sq];
        if (p == EMPTY) continue;

        bool is_white_piece = is_white((Piece)p);
        Side owner = is_white_piece ? WHITE : BLACK;
        Side attacker = is_white_piece ? BLACK : WHITE;

        int pt = piece_type((Piece)p);
        if (pt == WP || pt == WK) continue;

        bool attacked = board.is_square_attacked(sq, attacker);
        bool defended = board.is_square_attacked(sq, owner);

        if (attacked && !defended) {
            int val = piece_value(p);
            bool can_move_away = (owner == board.pos.side_to_move);
            int penalty = can_move_away ? val / 4 : val / 2;
            if (is_white_piece) {
                score -= penalty;
            } else {
                score += penalty;
            }
        }
    }
    return score;
}

static int evaluate_threats(Board &board) {
    int score = 0;
    int white_forks = count_knight_fork_threats(board, WHITE);
    int black_forks = count_knight_fork_threats(board, BLACK);
    score += white_forks * 300;
    score -= black_forks * 300;
    score += evaluate_hanging_pieces(board);
    return score;
}

// ========================
// King Safety — attack zone counting
// ========================

static int evaluate_king_safety(Board &board, int wk_sq, int bk_sq,
                                 bool white_has_queen, bool black_has_queen,
                                 int phase) {
    int safety = 0;
    bool is_endgame = phase < 80;

    // In endgame, king should centralize
    if (is_endgame) {
        if (wk_sq != -1) {
            int wf = file_of(wk_sq), wr = rank_of(wk_sq);
            int center_dist = std::max(abs(wf - 3), abs(wr - 3)) + std::max(abs(wf - 4), abs(wr - 4));
            safety += (6 - center_dist) * 8;
        }
        if (bk_sq != -1) {
            int bf = file_of(bk_sq), br = rank_of(bk_sq);
            int center_dist = std::max(abs(bf - 3), abs(br - 3)) + std::max(abs(bf - 4), abs(br - 4));
            safety -= (6 - center_dist) * 8;
        }
        return safety;
    }

    // === ATTACK ZONE EVALUATION ===
    // Count attackers near each king
    static const int king_zone_offsets[16][2] = {
        {-2,-1},{-2,0},{-2,1},
        {-1,-2},{-1,-1},{-1,0},{-1,1},{-1,2},
        {0,-2},{0,-1},{0,1},{0,2},
        {1,-2},{1,-1},{1,0},{1,1}
    };

    // White king safety (attacks by black)
    if (wk_sq != -1) {
        int wkf = file_of(wk_sq), wkr = rank_of(wk_sq);
        int attack_count = 0;
        int attack_weight = 0;
        for (auto &o : king_zone_offsets) {
            int nf = wkf + o[0], nr = wkr + o[1];
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
            int sq = make_sq(nf, nr);
            if (board.is_square_attacked(sq, BLACK)) {
                attack_count++;
                // Weight by proximity
                int dist = std::max(abs(o[0]), abs(o[1]));
                attack_weight += (dist <= 1) ? 3 : 1;
            }
        }
        // Quadratic scaling for king danger
        static const int safety_table[] = {0, 0, 1, 2, 3, 5, 7, 10, 13, 16, 20, 25, 30, 36, 42, 50, 58};
        int idx = clamp(attack_count, 0, 16);
        int danger = safety_table[idx] * 10;
        if (black_has_queen) danger = danger * 3 / 2;
        safety -= danger;

        // Pawn shield
        int shield_bonus = 0;
        for (int df = -1; df <= 1; ++df) {
            int sf = wkf + df;
            if (sf < 0 || sf > 7) continue;
            // Check rank 1 and 2 for pawns
            if (wkr <= 1) {
                if (board.pos.board[make_sq(sf, 1)] == WP) shield_bonus += 15;
                else if (board.pos.board[make_sq(sf, 2)] == WP) shield_bonus += 5;
                else shield_bonus -= 20;  // open file near king
            }
        }
        safety += shield_bonus;

        // Open files near king penalty
        for (int df = -1; df <= 1; ++df) {
            int sf = wkf + df;
            if (sf < 0 || sf > 7) continue;
            bool has_own_pawn = false;
            bool has_enemy_heavy = false;
            for (int r = 0; r < 8; ++r) {
                int p = board.pos.board[make_sq(sf, r)];
                if (p == WP) has_own_pawn = true;
                if (p == BR || p == BQ) has_enemy_heavy = true;
            }
            if (!has_own_pawn && has_enemy_heavy) safety -= 50;
        }
    }

    // Black king safety (attacks by white)
    if (bk_sq != -1) {
        int bkf = file_of(bk_sq), bkr = rank_of(bk_sq);
        int attack_count = 0;
        for (auto &o : king_zone_offsets) {
            int nf = bkf + o[0], nr = bkr + o[1];
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
            int sq = make_sq(nf, nr);
            if (board.is_square_attacked(sq, WHITE)) {
                attack_count++;
            }
        }
        static const int safety_table[] = {0, 0, 1, 2, 3, 5, 7, 10, 13, 16, 20, 25, 30, 36, 42, 50, 58};
        int idx = clamp(attack_count, 0, 16);
        int danger = safety_table[idx] * 10;
        if (white_has_queen) danger = danger * 3 / 2;
        safety += danger;

        // Pawn shield
        int shield_bonus = 0;
        for (int df = -1; df <= 1; ++df) {
            int sf = bkf + df;
            if (sf < 0 || sf > 7) continue;
            if (bkr >= 6) {
                if (board.pos.board[make_sq(sf, 6)] == BP) shield_bonus += 15;
                else if (board.pos.board[make_sq(sf, 5)] == BP) shield_bonus += 5;
                else shield_bonus -= 20;
            }
        }
        safety -= shield_bonus;

        // Open files near king penalty  
        for (int df = -1; df <= 1; ++df) {
            int sf = bkf + df;
            if (sf < 0 || sf > 7) continue;
            bool has_own_pawn = false;
            bool has_enemy_heavy = false;
            for (int r = 0; r < 8; ++r) {
                int p = board.pos.board[make_sq(sf, r)];
                if (p == BP) has_own_pawn = true;
                if (p == WR || p == WQ) has_enemy_heavy = true;
            }
            if (!has_own_pawn && has_enemy_heavy) safety += 50;
        }
    }

    // === KING POSITION PENALTIES — uncastled king in middlegame ===
    if (board.pos.fullmove_number > 6) {
        if (wk_sq != -1) {
            int wk_rank = rank_of(wk_sq);
            int wk_file = file_of(wk_sq);
            if (wk_rank >= 2 && wk_rank <= 5 && wk_file >= 2 && wk_file <= 5) {
                safety -= 120;
            }
            if (wk_sq == make_sq(4, 0) && board.pos.fullmove_number > 10) safety -= 30;
        }
        if (bk_sq != -1) {
            int bk_rank = rank_of(bk_sq);
            int bk_file = file_of(bk_sq);
            if (bk_rank >= 2 && bk_rank <= 5 && bk_file >= 2 && bk_file <= 5) {
                safety += 120;
            }
            if (bk_sq == make_sq(4, 7) && board.pos.fullmove_number > 10) safety += 30;
        }
    }

    return safety;
}

// ========================
// Mobility Evaluation
// ========================

static int evaluate_mobility(const Board &board) {
    int score = 0;
    static const int knight_offsets[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
    static const int bishop_dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    static const int rook_dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};

    for (int sq = 0; sq < 64; ++sq) {
        int p = board.pos.board[sq];
        if (p == EMPTY) continue;
        int f = file_of(sq), r = rank_of(sq);
        int mobility = 0;

        if (p == WN || p == BN) {
            for (auto &o : knight_offsets) {
                int nf = f + o[0], nr = r + o[1];
                if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
                int target = board.pos.board[make_sq(nf, nr)];
                if (target == EMPTY || is_opponent(p, target)) mobility++;
            }
            // Knight mobility bonus: 4 cp per square
            int bonus = (mobility - 4) * 4;
            score += is_white((Piece)p) ? bonus : -bonus;
        }
        else if (p == WB || p == BB) {
            for (auto &d : bishop_dirs) {
                int nf = f + d[0], nr = r + d[1];
                while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
                    int target = board.pos.board[make_sq(nf, nr)];
                    if (target == EMPTY) { mobility++; }
                    else { if (is_opponent(p, target)) mobility++; break; }
                    nf += d[0]; nr += d[1];
                }
            }
            // Bishop mobility bonus: 5 cp per square
            int bonus = (mobility - 5) * 5;
            score += is_white((Piece)p) ? bonus : -bonus;
        }
        else if (p == WR || p == BR) {
            for (auto &d : rook_dirs) {
                int nf = f + d[0], nr = r + d[1];
                while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
                    int target = board.pos.board[make_sq(nf, nr)];
                    if (target == EMPTY) { mobility++; }
                    else { if (is_opponent(p, target)) mobility++; break; }
                    nf += d[0]; nr += d[1];
                }
            }
            // Rook mobility bonus: 3 cp per square
            int bonus = (mobility - 7) * 3;
            score += is_white((Piece)p) ? bonus : -bonus;
        }
        else if (p == WQ || p == BQ) {
            for (auto &d : bishop_dirs) {
                int nf = f + d[0], nr = r + d[1];
                while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
                    int target = board.pos.board[make_sq(nf, nr)];
                    if (target == EMPTY) { mobility++; }
                    else { if (is_opponent(p, target)) mobility++; break; }
                    nf += d[0]; nr += d[1];
                }
            }
            for (auto &d : rook_dirs) {
                int nf = f + d[0], nr = r + d[1];
                while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
                    int target = board.pos.board[make_sq(nf, nr)];
                    if (target == EMPTY) { mobility++; }
                    else { if (is_opponent(p, target)) mobility++; break; }
                    nf += d[0]; nr += d[1];
                }
            }
            // Queen mobility bonus: 2 cp per square
            int bonus = (mobility - 14) * 2;
            score += is_white((Piece)p) ? bonus : -bonus;
        }
    }
    return score;
}

// ========================
// Development Quality
// ========================

static int evaluate_development_quality(Board &board) {
    int dev = 0;

    if (board.pos.fullmove_number <= 12) {
        int center_control = 0;
        int center_sqs[] = {make_sq(3,3), make_sq(3,4), make_sq(4,3), make_sq(4,4)};
        for (int csq : center_sqs) {
            if (board.is_square_attacked(csq, WHITE)) center_control += 8;
            if (board.is_square_attacked(csq, BLACK)) center_control -= 8;
            int p = board.pos.board[csq];
            if (p == WP || p == WN) center_control += 15;
            if (p == BP || p == BN) center_control -= 15;
        }
        dev += center_control;
    }

    return dev;
}

// ========================
// Pawn Structure
// ========================

static int evaluate_pawn_structure(const Board &board) {
    int score = 0;

    // Count pawns per file
    int white_pawns[8] = {};
    int black_pawns[8] = {};
    for (int sq = 0; sq < 64; ++sq) {
        int p = board.pos.board[sq];
        if (p == WP) white_pawns[file_of(sq)]++;
        else if (p == BP) black_pawns[file_of(sq)]++;
    }

    // Doubled pawns
    for (int f = 0; f < 8; ++f) {
        if (white_pawns[f] > 1) score -= (white_pawns[f] - 1) * 15;
        if (black_pawns[f] > 1) score += (black_pawns[f] - 1) * 15;
    }

    // Isolated pawns
    for (int f = 0; f < 8; ++f) {
        if (white_pawns[f] > 0) {
            bool has_neighbor = (f > 0 && white_pawns[f-1] > 0) || (f < 7 && white_pawns[f+1] > 0);
            if (!has_neighbor) score -= 12 * white_pawns[f];
        }
        if (black_pawns[f] > 0) {
            bool has_neighbor = (f > 0 && black_pawns[f-1] > 0) || (f < 7 && black_pawns[f+1] > 0);
            if (!has_neighbor) score += 12 * black_pawns[f];
        }
    }

    // Passed pawns
    for (int sq = 0; sq < 64; ++sq) {
        int p = board.pos.board[sq];
        if (p == WP) {
            int f = file_of(sq);
            int r = rank_of(sq);
            bool passed = true;
            for (int rr = r + 1; rr < 8 && passed; ++rr)
                for (int ff = std::max(0, f-1); ff <= std::min(7, f+1); ++ff)
                    if (board.pos.board[make_sq(ff, rr)] == BP) { passed = false; break; }
            if (passed) {
                // Bonus grows quadratically with rank
                static const int passed_bonus[] = {0, 5, 10, 20, 35, 60, 100, 0};
                score += passed_bonus[r];
            }
        } else if (p == BP) {
            int f = file_of(sq);
            int r = rank_of(sq);
            bool passed = true;
            for (int rr = r - 1; rr >= 0 && passed; --rr)
                for (int ff = std::max(0, f-1); ff <= std::min(7, f+1); ++ff)
                    if (board.pos.board[make_sq(ff, rr)] == WP) { passed = false; break; }
            if (passed) {
                static const int passed_bonus[] = {0, 100, 60, 35, 20, 10, 5, 0};
                score -= passed_bonus[r];
            }
        }
    }

    return score;
}

// ========================
// Main Evaluation — Tapered (MG/EG blend via game phase)
// ========================

int evaluate(Board &board) {
    int mg_score = 0;  // middlegame score
    int eg_score = 0;  // endgame score
    const auto &pos = board.pos;

    // === GAME PHASE CALCULATION ===
    // Phase based on non-pawn material (24 = opening, 0 = endgame)
    static const int phase_vals[] = {0, 0, 1, 1, 2, 4, 0}; // P N B R Q K
    int game_phase = 0;

    int white_material = 0, black_material = 0;
    int white_non_pawn = 0, black_non_pawn = 0;
    bool white_has_queen = false, black_has_queen = false;
    int wk_sq = -1, bk_sq = -1;
    int wq_sq = -1, bq_sq = -1;
    int white_pawns_count = 0, black_pawns_count = 0;

    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p == EMPTY) continue;
        if (p == WK) { wk_sq = sq; continue; }
        if (p == BK) { bk_sq = sq; continue; }
        int pt = piece_type((Piece)p);
        int val = 0;
        switch (pt) {
            case WP: val = 100; break;
            case WN: val = 320; break;
            case WB: val = 330; break;
            case WR: val = 500; break;
            case WQ: val = 900; break;
            default: break;
        }
        game_phase += phase_vals[pt];
        if (is_white((Piece)p)) {
            white_material += val;
            if (pt != WP) white_non_pawn += val;
            else white_pawns_count++;
            if (p == WQ) { white_has_queen = true; wq_sq = sq; }
        } else {
            black_material += val;
            if (pt != WP) black_non_pawn += val;
            else black_pawns_count++;
            if (p == BQ) { black_has_queen = true; bq_sq = sq; }
        }
    }

    // Phase: 0 (endgame) to 24 (opening)
    game_phase = clamp(game_phase, 0, 24);
    int mg_phase = game_phase;
    int eg_phase = 24 - game_phase;
    // Also compute a 0-256 phase for backward compat
    int phase256 = mg_phase * 256 / 24;

    // === MATERIAL + PST (Tapered) ===
    // MG material values
    static const int mg_value[] = {0, 82, 337, 365, 477, 1025, 0}; // P N B R Q K
    // EG material values  
    static const int eg_value[] = {0, 94, 281, 297, 512,  936, 0};

    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p == EMPTY) continue;
        bool white_piece = is_white((Piece)p);
        int pt = piece_type((Piece)p);
        int psq = white_piece ? sq : mirror_sq(sq);

        int mg_val = mg_value[pt];
        int eg_val = eg_value[pt];
        int mg_pst = 0, eg_pst = 0;

        switch (pt) {
            case WP: mg_pst = pst_pawn_mg[psq]; eg_pst = pst_pawn_eg[psq]; break;
            case WN: mg_pst = pst_knight_mg[psq]; eg_pst = pst_knight_eg[psq]; break;
            case WB: mg_pst = pst_bishop_mg[psq]; eg_pst = pst_bishop_eg[psq]; break;
            case WR: mg_pst = pst_rook_mg[psq]; eg_pst = pst_rook_eg[psq]; break;
            case WQ: mg_pst = pst_queen_mg[psq]; eg_pst = pst_queen_eg[psq]; break;
            case WK: mg_pst = pst_king_mg[psq]; eg_pst = pst_king_eg[psq]; break;
            default: break;
        }

        if (white_piece) {
            mg_score += mg_val + mg_pst;
            eg_score += eg_val + eg_pst;
        } else {
            mg_score -= mg_val + mg_pst;
            eg_score -= eg_val + eg_pst;
        }
    }

    // === BISHOP PAIR ===
    {
        int wb = 0, bb = 0;
        for (int sq = 0; sq < 64; ++sq) {
            if (pos.board[sq] == WB) wb++;
            if (pos.board[sq] == BB) bb++;
        }
        if (wb >= 2) { mg_score += 30; eg_score += 50; }
        if (bb >= 2) { mg_score -= 30; eg_score -= 50; }
    }

    // === TRADE INCENTIVE ===
    // When ahead, trade pieces; when behind, trade pawns
    int mat_diff = white_material - black_material;
    if (mat_diff > 0) {
        // White is ahead: encourage trading pieces
        mg_score += mat_diff * (6400 - black_non_pawn) / 6400 / 4;
        eg_score += mat_diff * (6400 - black_non_pawn) / 6400 / 3;
    } else if (mat_diff < 0) {
        mg_score += mat_diff * (6400 - white_non_pawn) / 6400 / 4;
        eg_score += mat_diff * (6400 - white_non_pawn) / 6400 / 3;
    }

    // === DEVELOPMENT (opening only) ===
    if (pos.fullmove_number <= 15) {
        int dev = 0;
        if (pos.board[make_sq(1,0)] != WN) dev += 12;
        if (pos.board[make_sq(6,0)] != WN) dev += 12;
        if (pos.board[make_sq(2,0)] != WB) dev += 10;
        if (pos.board[make_sq(5,0)] != WB) dev += 10;
        if (pos.board[make_sq(1,7)] != BN) dev -= 12;
        if (pos.board[make_sq(6,7)] != BN) dev -= 12;
        if (pos.board[make_sq(2,7)] != BB) dev -= 10;
        if (pos.board[make_sq(5,7)] != BB) dev -= 10;

        // Castling bonus
        if (pos.board[make_sq(6,0)] == WK || pos.board[make_sq(2,0)] == WK) dev += 40;
        if (pos.board[make_sq(6,7)] == BK || pos.board[make_sq(2,7)] == BK) dev -= 40;

        // Penalize early queen moves
        if (pos.fullmove_number <= 8) {
            if (pos.board[make_sq(3,0)] != WQ && white_has_queen) dev -= 20;
            if (pos.board[make_sq(3,7)] != BQ && black_has_queen) dev += 20;
        }

        // Penalty for undeveloped pieces in middlegame
        if (pos.fullmove_number > 8) {
            int pen = std::min(50, 8 + (pos.fullmove_number - 8) * 4);
            if (pos.board[make_sq(1,0)] == WN) dev -= pen;
            if (pos.board[make_sq(6,0)] == WN) dev -= pen;
            if (pos.board[make_sq(2,0)] == WB) dev -= pen;
            if (pos.board[make_sq(5,0)] == WB) dev -= pen;
            if (pos.board[make_sq(1,7)] == BN) dev += pen;
            if (pos.board[make_sq(6,7)] == BN) dev += pen;
            if (pos.board[make_sq(2,7)] == BB) dev += pen;
            if (pos.board[make_sq(5,7)] == BB) dev += pen;
        }

        mg_score += dev;
    }

    // === PAWN STRUCTURE ===
    {
        int wp[8] = {}, bp[8] = {};
        for (int sq = 0; sq < 64; ++sq) {
            int p = pos.board[sq];
            if (p == WP) wp[file_of(sq)]++;
            else if (p == BP) bp[file_of(sq)]++;
        }

        // Doubled pawns
        for (int f = 0; f < 8; ++f) {
            if (wp[f] > 1) { mg_score -= (wp[f]-1) * 12; eg_score -= (wp[f]-1) * 20; }
            if (bp[f] > 1) { mg_score += (bp[f]-1) * 12; eg_score += (bp[f]-1) * 20; }
        }

        // Isolated pawns
        for (int f = 0; f < 8; ++f) {
            if (wp[f] > 0) {
                bool neighbor = (f > 0 && wp[f-1] > 0) || (f < 7 && wp[f+1] > 0);
                if (!neighbor) { mg_score -= 15; eg_score -= 20; }
            }
            if (bp[f] > 0) {
                bool neighbor = (f > 0 && bp[f-1] > 0) || (f < 7 && bp[f+1] > 0);
                if (!neighbor) { mg_score += 15; eg_score += 20; }
            }
        }

        // Passed pawns
        for (int sq = 0; sq < 64; ++sq) {
            int p = pos.board[sq];
            if (p == WP) {
                int f = file_of(sq), r = rank_of(sq);
                bool passed = true;
                for (int rr = r + 1; rr < 8 && passed; ++rr)
                    for (int ff = std::max(0, f-1); ff <= std::min(7, f+1); ++ff)
                        if (pos.board[make_sq(ff, rr)] == BP) { passed = false; break; }
                if (passed) {
                    static const int mg_bonus[] = {0, 5, 12, 22, 40, 65, 120, 0};
                    static const int eg_bonus[] = {0, 10, 20, 40, 70, 120, 200, 0};
                    mg_score += mg_bonus[r];
                    eg_score += eg_bonus[r];
                    // Bonus for connected passed pawns
                    if (f > 0 && pos.board[make_sq(f-1, r)] == WP) {
                        mg_score += mg_bonus[r] / 2;
                        eg_score += eg_bonus[r] / 2;
                    }
                    if (f < 7 && pos.board[make_sq(f+1, r)] == WP) {
                        mg_score += mg_bonus[r] / 2;
                        eg_score += eg_bonus[r] / 2;
                    }
                }
            } else if (p == BP) {
                int f = file_of(sq), r = rank_of(sq);
                bool passed = true;
                for (int rr = r - 1; rr >= 0 && passed; --rr)
                    for (int ff = std::max(0, f-1); ff <= std::min(7, f+1); ++ff)
                        if (pos.board[make_sq(ff, rr)] == WP) { passed = false; break; }
                if (passed) {
                    static const int mg_bonus[] = {0, 120, 65, 40, 22, 12, 5, 0};
                    static const int eg_bonus[] = {0, 200, 120, 70, 40, 20, 10, 0};
                    mg_score -= mg_bonus[r];
                    eg_score -= eg_bonus[r];
                    // Connected 
                    if (f > 0 && pos.board[make_sq(f-1, r)] == BP) {
                        mg_score -= mg_bonus[r] / 2;
                        eg_score -= eg_bonus[r] / 2;
                    }
                    if (f < 7 && pos.board[make_sq(f+1, r)] == BP) {
                        mg_score -= mg_bonus[r] / 2;
                        eg_score -= eg_bonus[r] / 2;
                    }
                }
            }
        }
    }

    // === KING SAFETY ===
    int ks = evaluate_king_safety(board, wk_sq, bk_sq, white_has_queen, black_has_queen, phase256);
    mg_score += ks;

    // === DEVELOPMENT QUALITY ===
    mg_score += evaluate_development_quality(board);

    // === MOBILITY ===
    int mob = evaluate_mobility(board);
    mg_score += mob;
    eg_score += mob;

    // === TACTICAL THREATS ===
    mg_score += evaluate_threats(board);

    // === ROOK ON OPEN/SEMI-OPEN FILE ===
    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p != WR && p != BR) continue;
        int f = file_of(sq);
        bool has_own_pawn = false;
        bool has_opp_pawn = false;
        for (int r = 0; r < 8; ++r) {
            int pp = pos.board[make_sq(f, r)];
            if (p == WR) {
                if (pp == WP) has_own_pawn = true;
                if (pp == BP) has_opp_pawn = true;
            } else {
                if (pp == BP) has_own_pawn = true;
                if (pp == WP) has_opp_pawn = true;
            }
        }
        int mg_bonus = 0, eg_bonus_r = 0;
        if (!has_own_pawn && !has_opp_pawn) { mg_bonus = 35; eg_bonus_r = 25; }
        else if (!has_own_pawn) { mg_bonus = 20; eg_bonus_r = 15; }
        if (p == WR) { mg_score += mg_bonus; eg_score += eg_bonus_r; }
        else { mg_score -= mg_bonus; eg_score -= eg_bonus_r; }
    }

    // === ROOK ON 7th RANK ===
    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p == WR && rank_of(sq) == 6) { mg_score += 30; eg_score += 40; }
        if (p == BR && rank_of(sq) == 1) { mg_score -= 30; eg_score -= 40; }
    }

    // === CONNECTED ROOKS ===
    {
        int wr[2] = {-1, -1}, br[2] = {-1, -1};
        int wri = 0, bri = 0;
        for (int sq = 0; sq < 64; ++sq) {
            if (pos.board[sq] == WR && wri < 2) wr[wri++] = sq;
            if (pos.board[sq] == BR && bri < 2) br[bri++] = sq;
        }
        if (wri == 2 && (rank_of(wr[0]) == rank_of(wr[1]) || file_of(wr[0]) == file_of(wr[1]))) {
            // Check if connected (no pieces between)
            mg_score += 10; eg_score += 15;
        }
        if (bri == 2 && (rank_of(br[0]) == rank_of(br[1]) || file_of(br[0]) == file_of(br[1]))) {
            mg_score -= 10; eg_score -= 15;
        }
    }

    // === MOP-UP EVALUATION (endgame with large material advantage) ===
    if (eg_phase > 16) {
        int total_mat = white_material + black_material;
        if (total_mat > 0) {
            int advantage = white_material - black_material;
            if (abs(advantage) >= 300) {
                // Drive losing king to corner
                int losing_king_sq = (advantage > 0) ? bk_sq : wk_sq;
                int winning_king_sq = (advantage > 0) ? wk_sq : bk_sq;
                if (losing_king_sq >= 0 && winning_king_sq >= 0) {
                    int lkf = file_of(losing_king_sq), lkr = rank_of(losing_king_sq);
                    int wkf = file_of(winning_king_sq), wkr = rank_of(winning_king_sq);
                    // Distance of losing king from center
                    int center_dist = std::max(abs(lkf * 2 - 7), abs(lkr * 2 - 7));
                    // Distance between kings
                    int king_dist = abs(wkf - lkf) + abs(wkr - lkr);
                    int mopup = center_dist * 10 + (14 - king_dist) * 5;
                    if (advantage > 0) eg_score += mopup;
                    else eg_score -= mopup;
                }
            }
        }
    }

    // === TAPERED EVAL ===
    int score = (mg_score * mg_phase + eg_score * eg_phase) / 24;

    return (pos.side_to_move == WHITE) ? score : -score;
}

} // namespace chess
