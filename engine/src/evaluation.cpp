#include "evaluation.h"
#include "position.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace chess {

// ========================
// PST Tables (file-scope)
// ========================

static std::array<int, 64> pst_pawn;
static std::array<int, 64> pst_knight;
static std::array<int, 64> pst_bishop;
static std::array<int, 64> pst_rook;
static std::array<int, 64> pst_queen;
static std::array<int, 64> pst_king;
static std::array<int, 64> pst_king_endgame;

void init_pst_tables() {
    pst_pawn = {0, 0, 0, 0, 0, 0, 0, 0,
                5, 5, 5, 5, 5, 5, 5, 5,
                1, 1, 2, 3, 3, 2, 1, 1,
                0, 0, 0, 2, 2, 0, 0, 0,
                1, 1, 1, -2, -2, 1, 1, 1,
                1, 1, 1, -3, -3, 1, 1, 1,
                5, 5, 5, -5, -5, 5, 5, 5,
                0, 0, 0, 0, 0, 0, 0, 0};

    pst_knight = {-50, -40, -30, -30, -30, -30, -40, -50,
              -40, -20,  0,   0,   0,   0,  -20, -40,
              -30,  0,  10,  15,  15,  10,   0,  -30,
              -30,  5,  15,  20,  20,  15,   5,  -30,
              -30,  0,  15,  20,  20,  15,   0,  -30,
              -30,  5,  10,  15,  15,  10,   5,  -30,
              -40, -20,  0,   5,   5,   0,  -20, -40,
              -50, -40, -30, -30, -30, -30, -40, -50};

    pst_bishop = {-2, -1, -1, -1, -1, -1, -1, -2,
              -1, 0, 0, 0, 0, 0, 0, -1,
              -1, 0, 1, 1, 1, 1, 0, -1,
              -1, 1, 1, 1, 1, 1, 1, -1,
              -1, 0, 1, 1, 1, 1, 0, -1,
              -1, 1, 1, 1, 1, 1, 1, -1,
              -1, 1, 0, 0, 0, 0, 1, -1,
              -2, -1, -1, -1, -1, -1, -1, -2};

    pst_rook = {0, 0, 0, 0, 0, 0, 0, 0,
            1, 2, 2, 2, 2, 2, 2, 1,
            -1, 0, 0, 0, 0, 0, 0, -1,
            -1, 0, 0, 0, 0, 0, 0, -1,
            -1, 0, 0, 0, 0, 0, 0, -1,
            -1, 0, 0, 0, 0, 0, 0, -1,
            -1, 0, 0, 0, 0, 0, 0, -1,
            0, 0, 0, 1, 1, 0, 0, 0};

    pst_queen = {-2, -1, -1, -1, -1, -1, -1, -2,
             -1, 0, 0, 0, 0, 0, 0, -1,
             -1, 0, 1, 1, 1, 1, 0, -1,
             -1, 0, 1, 1, 1, 1, 0, -1,
             0, 0, 1, 1, 1, 1, 0, -1,
             -1, 1, 1, 1, 1, 1, 0, -1,
             -1, 0, 1, 0, 0, 0, 0, -1,
             -2, -1, -1, -1, -1, -1, -1, -2};

    pst_king = {-3, -4, -4, -5, -5, -4, -4, -3,
                -3, -4, -4, -5, -5, -4, -4, -3,
                -3, -4, -4, -5, -5, -4, -4, -3,
                -3, -4, -4, -5, -5, -4, -4, -3,
                -2, -3, -3, -4, -4, -3, -3, -2,
                -1, -2, -2, -2, -2, -2, -2, -1,
                2, 2, 0, 0, 0, 0, 2, 2,
                2, 3, 1, 0, 0, 1, 3, 2};

    pst_king_endgame = {-50, -30, -20, -20, -20, -20, -30, -50,
                        -30, -10,   0,   5,   5,   0, -10, -30,
                        -20,   0,  10,  15,  15,  10,   0, -20,
                        -20,   5,  15,  20,  20,  15,   5, -20,
                        -20,   5,  15,  20,  20,  15,   5, -20,
                        -20,   0,  10,  15,  15,  10,   0, -20,
                        -30, -10,   0,   5,   5,   0, -10, -30,
                        -50, -30, -20, -20, -20, -20, -30, -50};
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
// King Safety
// ========================

static int evaluate_king_safety(Board &board) {
    int safety = 0;

    int non_pawn_material = 0;
    bool white_has_queen = false, black_has_queen = false;
    int wk_sq = -1, bk_sq = -1;
    for (int sq = 0; sq < 64; ++sq) {
        int p = board.pos.board[sq];
        if (p == WK) { wk_sq = sq; continue; }
        if (p == BK) { bk_sq = sq; continue; }
        if (p == EMPTY) continue;
        int pt = piece_type((Piece)p);
        if (p == WQ) white_has_queen = true;
        if (p == BQ) black_has_queen = true;
        if (pt != WP) {
            switch (pt) {
                case WN: non_pawn_material += 320; break;
                case WB: non_pawn_material += 330; break;
                case WR: non_pawn_material += 500; break;
                case WQ: non_pawn_material += 900; break;
                default: break;
            }
        }
    }

    bool is_endgame = (non_pawn_material < 2000) || (!white_has_queen && !black_has_queen);

    if (is_endgame) {
        if (wk_sq != -1) {
            int wf = file_of(wk_sq), wr = rank_of(wk_sq);
            int center_dist = std::max(abs(wf - 3), abs(wr - 3));
            safety += (3 - center_dist) * 15;
        }
        if (bk_sq != -1) {
            int bf = file_of(bk_sq), br = rank_of(bk_sq);
            int center_dist = std::max(abs(bf - 3), abs(br - 3));
            safety -= (3 - center_dist) * 15;
        }
        return safety;
    }

    // === PAWN SHIELD FOR CASTLED KING ===
    // White kingside castle
    if (wk_sq != -1) {
        int wkf = file_of(wk_sq), wkr = rank_of(wk_sq);
        if (wkr == 0 && (wkf == 6 || wkf == 5)) {
            if (board.pos.board[make_sq(5, 1)] != WP) safety -= 30;
            if (board.pos.board[make_sq(6, 1)] != WP) safety -= 30;
            if (board.pos.board[make_sq(7, 1)] != WP) safety -= 20;
            if (board.pos.board[make_sq(5, 2)] == WP && board.pos.board[make_sq(5, 1)] != WP) safety -= 15;
            if (board.pos.board[make_sq(6, 2)] == WP && board.pos.board[make_sq(6, 1)] != WP) safety -= 10;
            if (black_has_queen) {
                if (board.pos.board[make_sq(5, 1)] != WP) safety -= 25;
                if (board.pos.board[make_sq(6, 1)] != WP) safety -= 25;
            }
            for (int f = 5; f <= 7; ++f) {
                bool has_own_pawn = false;
                bool has_enemy_heavy = false;
                for (int r = 0; r < 8; ++r) {
                    int p = board.pos.board[make_sq(f, r)];
                    if (p == WP) has_own_pawn = true;
                    if (p == BR || p == BQ) has_enemy_heavy = true;
                }
                if (!has_own_pawn) {
                    safety -= 60;
                    if (has_enemy_heavy) safety -= 80;
                }
            }
        }
        // White queenside castle
        if (wkr == 0 && (wkf == 2 || wkf == 1)) {
            if (board.pos.board[make_sq(0, 1)] != WP) safety -= 20;
            if (board.pos.board[make_sq(1, 1)] != WP) safety -= 30;
            if (board.pos.board[make_sq(2, 1)] != WP) safety -= 30;
            if (black_has_queen && board.pos.board[make_sq(2, 1)] != WP) safety -= 20;
            for (int f = 0; f <= 2; ++f) {
                bool has_own_pawn = false;
                bool has_enemy_heavy = false;
                for (int r = 0; r < 8; ++r) {
                    int p = board.pos.board[make_sq(f, r)];
                    if (p == WP) has_own_pawn = true;
                    if (p == BR || p == BQ) has_enemy_heavy = true;
                }
                if (!has_own_pawn) {
                    safety -= 60;
                    if (has_enemy_heavy) safety -= 80;
                }
            }
        }
    }
    // Black kingside castle
    if (bk_sq != -1) {
        int bkf = file_of(bk_sq), bkr = rank_of(bk_sq);
        if (bkr == 7 && (bkf == 6 || bkf == 5)) {
            if (board.pos.board[make_sq(5, 6)] != BP) safety += 30;
            if (board.pos.board[make_sq(6, 6)] != BP) safety += 30;
            if (board.pos.board[make_sq(7, 6)] != BP) safety += 20;
            if (board.pos.board[make_sq(5, 5)] == BP && board.pos.board[make_sq(5, 6)] != BP) safety += 15;
            if (board.pos.board[make_sq(6, 5)] == BP && board.pos.board[make_sq(6, 6)] != BP) safety += 10;
            if (white_has_queen) {
                if (board.pos.board[make_sq(5, 6)] != BP) safety += 25;
                if (board.pos.board[make_sq(6, 6)] != BP) safety += 25;
            }
            for (int f = 5; f <= 7; ++f) {
                bool has_own_pawn = false;
                bool has_enemy_heavy = false;
                for (int r = 0; r < 8; ++r) {
                    int p = board.pos.board[make_sq(f, r)];
                    if (p == BP) has_own_pawn = true;
                    if (p == WR || p == WQ) has_enemy_heavy = true;
                }
                if (!has_own_pawn) {
                    safety += 60;
                    if (has_enemy_heavy) safety += 80;
                }
            }
        }
        if (bkr == 7 && (bkf == 2 || bkf == 1)) {
            if (board.pos.board[make_sq(0, 6)] != BP) safety += 20;
            if (board.pos.board[make_sq(1, 6)] != BP) safety += 30;
            if (board.pos.board[make_sq(2, 6)] != BP) safety += 30;
            if (white_has_queen && board.pos.board[make_sq(2, 6)] != BP) safety += 20;
            for (int f = 0; f <= 2; ++f) {
                bool has_own_pawn = false;
                bool has_enemy_heavy = false;
                for (int r = 0; r < 8; ++r) {
                    int p = board.pos.board[make_sq(f, r)];
                    if (p == BP) has_own_pawn = true;
                    if (p == WR || p == WQ) has_enemy_heavy = true;
                }
                if (!has_own_pawn) {
                    safety += 60;
                    if (has_enemy_heavy) safety += 80;
                }
            }
        }
    }

    // === KING POSITION PENALTIES ===
    if (wk_sq != -1) {
        int wk_rank = rank_of(wk_sq);
        int wk_file = file_of(wk_sq);

        if (wk_rank >= 2 && wk_rank <= 5) {
            safety -= 300;
            if (wk_file >= 2 && wk_file <= 5) safety -= 200;
            if (board.is_square_attacked(wk_sq, BLACK)) safety -= 150;
        } else if (wk_rank == 1) {
            safety -= 200;
            if (wk_file >= 2 && wk_file <= 5) safety -= 100;
            if (board.is_square_attacked(wk_sq, BLACK)) safety -= 150;
        } else if (wk_rank == 0 && wk_file != 6 && wk_file != 2 && wk_file != 4) {
            safety -= 50;
        }
    }

    if (bk_sq != -1) {
        int bk_rank = rank_of(bk_sq);
        int bk_file = file_of(bk_sq);

        if (bk_rank >= 2 && bk_rank <= 5) {
            safety += 300;
            if (bk_file >= 2 && bk_file <= 5) safety += 200;
            if (board.is_square_attacked(bk_sq, WHITE)) safety += 150;
        } else if (bk_rank == 6) {
            safety += 200;
            if (bk_file >= 2 && bk_file <= 5) safety += 100;
            if (board.is_square_attacked(bk_sq, WHITE)) safety += 150;
        } else if (bk_rank == 7 && bk_file != 6 && bk_file != 2 && bk_file != 4) {
            safety += 50;
        }
    }

    // === F7 weakness for Black ===
    int f7 = make_sq(5, 6);
    if (bk_sq == make_sq(4, 7)) {
        if (board.pos.board[f7] == BP) {
            if (board.is_square_attacked(f7, WHITE)) safety += 80;
            if (board.pos.board[make_sq(4, 6)] == BN) safety += 25;
        } else if (board.pos.board[f7] == EMPTY) {
            safety += 60;
        }
    }

    // === F2 weakness for White ===
    int f2 = make_sq(5, 1);
    if (wk_sq == make_sq(4, 0)) {
        if (board.pos.board[f2] == WP) {
            if (board.is_square_attacked(f2, BLACK)) safety -= 80;
        } else if (board.pos.board[f2] == EMPTY) {
            safety -= 60;
        }
    }

    // === Penalty for king in center after move 8 ===
    if (board.pos.fullmove_number > 8) {
        if (wk_sq == make_sq(4, 0)) safety -= 40;
        if (bk_sq == make_sq(4, 7)) safety += 40;
    }

    // === Qxf7# pattern ===
    int e5 = make_sq(4, 4);
    if (board.pos.board[e5] == WP || board.is_square_attacked(e5, WHITE)) {
        if (bk_sq == make_sq(4, 7) && board.pos.fullmove_number <= 15) {
            safety += 30;
        }
    }

    return safety;
}

// ========================
// Development Quality
// ========================

static int evaluate_development_quality(Board &board) {
    int dev = 0;

    if (board.pos.fullmove_number <= 12) {
        for (int sq = 0; sq < 64; ++sq) {
            int p = board.pos.board[sq];
            int f = file_of(sq);
            int r = rank_of(sq);

            if ((p == WB || p == BB) && (f == 0 || f == 7)) {
                if (p == WB) dev -= 35;
                else dev += 35;
            }

            if (p == WB && (sq == make_sq(0, 1) || sq == make_sq(1, 0))) dev -= 50;
            if (p == BB && (sq == make_sq(0, 6) || sq == make_sq(1, 7))) dev += 50;
            if (p == BB && sq == make_sq(0, 1)) dev += 80;
            if (p == WB && sq == make_sq(0, 6)) dev -= 80;

            if (p == BN || p == BB) {
                if (sq == make_sq(4, 6) && board.pos.board[make_sq(4, 4)] != BP) dev += 20;
                if (sq == make_sq(3, 6) && board.pos.board[make_sq(3, 4)] != BP) dev += 20;
            }
            if (p == WN || p == WB) {
                if (sq == make_sq(4, 1) && board.pos.board[make_sq(4, 3)] != WP) dev -= 20;
                if (sq == make_sq(3, 1) && board.pos.board[make_sq(3, 3)] != WP) dev -= 20;
            }
        }

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
// Main Evaluation
// ========================

int evaluate(Board &board) {
    int score = 0;
    const auto &pos = board.pos;

    // === GAME PHASE ===
    int white_material = 0, black_material = 0;
    int white_non_pawn = 0, black_non_pawn = 0;
    bool white_has_queen = false, black_has_queen = false;
    int wk_sq = -1, bk_sq = -1;
    int wq_sq = -1, bq_sq = -1;

    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p == EMPTY) continue;
        if (p == WK) { wk_sq = sq; continue; }
        if (p == BK) { bk_sq = sq; continue; }
        int val = 0;
        switch (piece_type((Piece)p)) {
            case WP: val = 100; break;
            case WN: val = 320; break;
            case WB: val = 330; break;
            case WR: val = 500; break;
            case WQ: val = 900; break;
            default: break;
        }
        if (is_white((Piece)p)) {
            white_material += val;
            if (piece_type((Piece)p) != WP) white_non_pawn += val;
            if (p == WQ) { white_has_queen = true; wq_sq = sq; }
        } else {
            black_material += val;
            if (piece_type((Piece)p) != WP) black_non_pawn += val;
            if (p == BQ) { black_has_queen = true; bq_sq = sq; }
        }
    }

    int total_non_pawn = white_non_pawn + black_non_pawn;
    int phase = clamp(total_non_pawn * 256 / 6400, 0, 256);
    bool is_endgame = (phase < 100) || (!white_has_queen && !black_has_queen);

    // === MATERIAL + PST ===
    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p == EMPTY) continue;
        bool white_piece = p <= WK;
        int psq = white_piece ? sq : mirror_sq(sq);
        int val = 0;
        switch (piece_type((Piece)p)) {
            case WP: {
                val = 100 + pst_pawn[psq];
                int rank = rank_of(sq);
                if (white_piece && rank == 6) val += 500;
                if (!white_piece && rank == 1) val += 500;
                if (is_endgame) {
                    if (white_piece && rank >= 4) val += (rank - 3) * 40;
                    if (!white_piece && rank <= 3) val += (4 - rank) * 40;
                }
                break;
            }
            case WN: val = 320 + pst_knight[psq]; break;
            case WB: val = 330 + pst_bishop[psq]; break;
            case WR: val = 500 + pst_rook[psq]; break;
            case WQ: val = 900 + pst_queen[psq]; break;
            case WK: {
                int mg_val = pst_king[psq];
                int eg_val = pst_king_endgame[psq];
                val = 20000 + (mg_val * phase + eg_val * (256 - phase)) / 256;
                break;
            }
            default: break;
        }
        if (p >= BP) val = -val;
        score += val;
    }

    // === TRADE INCENTIVE ===
    int mat_diff = white_material - black_material;
    int total_mat = white_material + black_material;
    if (total_mat > 0) {
        score += mat_diff * (8000 - total_mat) / 8000;
    }

    // === DEVELOPMENT ===
    int dev = 0;
    if (pos.fullmove_number <= 12) {
        if (pos.board[make_sq(1,0)] != WN) dev += 15;
        if (pos.board[make_sq(6,0)] != WN) dev += 15;
        if (pos.board[make_sq(2,0)] != WB) dev += 12;
        if (pos.board[make_sq(5,0)] != WB) dev += 12;
        if (pos.board[make_sq(1,7)] != BN) dev -= 15;
        if (pos.board[make_sq(6,7)] != BN) dev -= 15;
        if (pos.board[make_sq(2,7)] != BB) dev -= 12;
        if (pos.board[make_sq(5,7)] != BB) dev -= 12;

        if (pos.board[make_sq(3,0)] != WQ && pos.fullmove_number <= 8) dev -= 25;
        if (pos.board[make_sq(3,7)] != BQ && pos.fullmove_number <= 8) dev += 25;

        if (pos.board[make_sq(1,0)] == WR) dev -= 60;
        if (pos.board[make_sq(6,0)] == WR) dev -= 60;
        if (pos.board[make_sq(1,7)] == BR) dev += 60;
        if (pos.board[make_sq(6,7)] == BR) dev += 60;

        if (pos.board[make_sq(2,0)] == WR && pos.fullmove_number <= 6) dev -= 30;
        if (pos.board[make_sq(5,0)] == WR && pos.fullmove_number <= 6) dev -= 30;
        if (pos.board[make_sq(2,7)] == BR && pos.fullmove_number <= 6) dev += 30;
        if (pos.board[make_sq(5,7)] == BR && pos.fullmove_number <= 6) dev += 30;
    }

    // Castling bonus
    if (pos.fullmove_number <= 30) {
        if (pos.board[make_sq(6,0)] == WK || pos.board[make_sq(2,0)] == WK) dev += 60;
        if (pos.board[make_sq(6,7)] == BK || pos.board[make_sq(2,7)] == BK) dev -= 60;
    }

    score += dev;

    // === QUEEN OUT OF POSITION ===
    if (phase > 80) {
        if (wq_sq != -1 && wk_sq != -1) {
            int q_dist = abs(file_of(wq_sq) - file_of(wk_sq)) + abs(rank_of(wq_sq) - rank_of(wk_sq));
            if (q_dist > 4) {
                int undeveloped = 0;
                if (pos.board[make_sq(1,0)] == WN) undeveloped++;
                if (pos.board[make_sq(6,0)] == WN) undeveloped++;
                if (pos.board[make_sq(2,0)] == WB) undeveloped++;
                if (pos.board[make_sq(5,0)] == WB) undeveloped++;
                int excess = q_dist - 4;
                int penalty = excess * (20 + undeveloped * 35);
                if (black_has_queen) penalty += excess * 12;
                int wq_rank = rank_of(wq_sq);
                if (wq_rank >= 6) penalty += 100 + undeveloped * 80;
                score -= penalty;
            }
        }

        if (bq_sq != -1 && bk_sq != -1) {
            int q_dist = abs(file_of(bq_sq) - file_of(bk_sq)) + abs(rank_of(bq_sq) - rank_of(bk_sq));
            if (q_dist > 4) {
                int undeveloped = 0;
                if (pos.board[make_sq(1,7)] == BN) undeveloped++;
                if (pos.board[make_sq(6,7)] == BN) undeveloped++;
                if (pos.board[make_sq(2,7)] == BB) undeveloped++;
                if (pos.board[make_sq(5,7)] == BB) undeveloped++;
                int excess = q_dist - 4;
                int penalty = excess * (20 + undeveloped * 35);
                if (white_has_queen) penalty += excess * 12;
                int bq_rank = rank_of(bq_sq);
                if (bq_rank <= 1) penalty += 100 + undeveloped * 80;
                score += penalty;
            }
        }
    }

    // === UNDEVELOPED PIECES IN MIDDLEGAME ===
    if (pos.fullmove_number > 8 && phase > 60) {
        int pen = std::min(60, 10 + (pos.fullmove_number - 8) * 4);
        if (pos.board[make_sq(1,0)] == WN) score -= pen;
        if (pos.board[make_sq(6,0)] == WN) score -= pen;
        if (pos.board[make_sq(2,0)] == WB) score -= pen;
        if (pos.board[make_sq(5,0)] == WB) score -= pen;
        if (pos.board[make_sq(1,7)] == BN) score += pen;
        if (pos.board[make_sq(6,7)] == BN) score += pen;
        if (pos.board[make_sq(2,7)] == BB) score += pen;
        if (pos.board[make_sq(5,7)] == BB) score += pen;
    }

    // === KING SAFETY ===
    score += evaluate_king_safety(board);

    // === DEVELOPMENT QUALITY ===
    score += evaluate_development_quality(board);

    // === TACTICAL THREATS ===
    score += evaluate_threats(board);

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
        int bonus = 0;
        if (!has_own_pawn && !has_opp_pawn) bonus = 25;
        else if (!has_own_pawn) bonus = 15;
        if (p == WR) score += bonus;
        else score -= bonus;
    }

    // === ROOK ON 7th RANK ===
    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p == WR && rank_of(sq) == 6) score += 30;
        if (p == BR && rank_of(sq) == 1) score -= 30;
    }

    // === BISHOP PAIR ===
    {
        int wb = 0, bb = 0;
        for (int sq = 0; sq < 64; ++sq) {
            if (pos.board[sq] == WB) wb++;
            if (pos.board[sq] == BB) bb++;
        }
        if (wb >= 2) score += 30;
        if (bb >= 2) score -= 30;
    }

    // === PAWN STRUCTURE ===
    score += evaluate_pawn_structure(board);

    return (pos.side_to_move == WHITE) ? score : -score;
}

} // namespace chess
