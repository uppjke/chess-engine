#include "search.h"
#include "position.h"
#include "movegen.h"
#include "evaluation.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace std;

namespace chess {

Searcher::Searcher() {
    opening_rng.seed(chrono::steady_clock::now().time_since_epoch().count());
}

void Searcher::new_game() {
    tt.clear();
    opening_rng.seed(chrono::steady_clock::now().time_since_epoch().count());
}

// ========================
// Time Management
// ========================

bool Searcher::time_up() {
    if (stop_search) return true;
    auto now = chrono::steady_clock::now();
    int elapsed = (int)chrono::duration_cast<chrono::milliseconds>(now - search_start).count();
    if (elapsed >= time_limit) {
        stop_search = true;
        return true;
    }
    return false;
}

// ========================
// Move Ordering Helpers
// ========================

bool Searcher::is_reverse_of_last(const Board &board, const Move &m) const {
    const Move &lm = (board.pos.side_to_move == WHITE) ? board.last_move_white : board.last_move_black;
    if (lm.from == -1) return false;
    if (m.captured != EMPTY || lm.captured != EMPTY) return false;
    return (m.from == lm.to && m.to == lm.from);
}

int Searcher::reverse_move_penalty(const Move &m) const {
    int pen = 80;
    int pt = piece_type((Piece)m.moved);
    if (pt == WR) pen = 180;
    if (pt == WQ) pen = 140;
    return pen;
}

bool Searcher::is_repeat_piece_move(const Board &board, const Move &m) const {
    const Move &lm = (board.pos.side_to_move == WHITE) ? board.last_move_white : board.last_move_black;
    if (lm.from == -1) return false;
    if (m.captured != EMPTY || lm.captured != EMPTY) return false;
    if (m.moved != lm.moved || m.from != lm.to) return false;
    Side opp = (board.pos.side_to_move == WHITE) ? BLACK : WHITE;
    if (board.is_square_attacked(m.from, opp)) return false;
    return true;
}

int Searcher::see_score(const Board &board, const Move &m) const {
    Side mover = (m.moved <= WK) ? WHITE : BLACK;
    Side opp = (mover == WHITE) ? BLACK : WHITE;
    int gain = (m.captured != EMPTY) ? piece_value(m.captured) : 0;
    int cost = piece_value(m.moved);
    bool attacked = board.is_square_attacked(m.to, opp);
    bool defended = board.is_square_attacked(m.to, mover);
    if (attacked && !defended) return gain - cost;
    if (attacked && defended) return gain - (cost / 2);
    return gain;
}

// ========================
// Move Scoring (for ordering)
// ========================

int Searcher::move_score(const Board &board, const Move &m) const {
    int score = 0;
    const auto &pos = board.pos;

    // === CRITICAL: Check if we can capture enemy QUEEN ===
    {
        int cap_pt = piece_type((Piece)m.captured);
        if (cap_pt == WQ) {
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;
            int our_value = piece_value(m.moved);

            if (!board.is_square_attacked(m.to, opp)) {
                score += 5000;
            } else {
                if (our_value < 900) {
                    score += 3000;
                }
            }
        }
    }

    // === CRITICAL: Check if any of OUR pieces is hanging ===
    {
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;

        int worst_threat_sq = -1;
        int worst_threat_value = 0;

        for (int sq = 0; sq < 64; ++sq) {
            int p = pos.board[sq];
            if (p == EMPTY) continue;
            bool our_piece = (mover == WHITE) ? is_white((Piece)p) : is_black((Piece)p);
            if (!our_piece) continue;
            if (piece_type((Piece)p) == WK) continue;

            int our_val = piece_value(p);

            if (board.is_square_attacked(sq, opp)) {
                int min_attacker_val = 10000;
                for (int asq = 0; asq < 64; ++asq) {
                    int ap = pos.board[asq];
                    if (ap == EMPTY) continue;
                    bool enemy = (opp == WHITE) ? is_white((Piece)ap) : is_black((Piece)ap);
                    if (!enemy) continue;

                    int apf = file_of(asq);
                    int apr = rank_of(asq);
                    int sf = file_of(sq);
                    int sr = rank_of(sq);
                    int pt_a = piece_type((Piece)ap);

                    if (pt_a == WP) {
                        if (opp == WHITE && apr + 1 == sr && abs(apf - sf) == 1) {
                            min_attacker_val = 100;
                        }
                        if (opp == BLACK && apr - 1 == sr && abs(apf - sf) == 1) {
                            min_attacker_val = 100;
                        }
                    }
                }

                bool is_defended = board.is_square_attacked(sq, mover);
                bool is_threatened = !is_defended || (min_attacker_val < our_val - 50);

                if (is_threatened) {
                    int threat_value = is_defended ? (our_val - min_attacker_val) : our_val;
                    if (threat_value > worst_threat_value) {
                        worst_threat_value = threat_value;
                        worst_threat_sq = sq;
                    }
                }
            }
        }

        if (worst_threat_sq != -1) {
            bool saves_piece = (m.from == worst_threat_sq);

            if (saves_piece) {
                if (!board.is_square_attacked(m.to, opp)) {
                    score += worst_threat_value * 3;
                } else {
                    score += worst_threat_value;
                }
            } else if (m.captured == EMPTY) {
                score -= worst_threat_value * 3;
            }
        }
    }

    // === CRITICAL: Check if our queen is under attack ===
    {
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;
        int our_queen = (mover == WHITE) ? WQ : BQ;
        int our_queen_sq = -1;

        for (int sq = 0; sq < 64; ++sq) {
            if (pos.board[sq] == our_queen) {
                our_queen_sq = sq;
                break;
            }
        }

        if (our_queen_sq != -1 && board.is_square_attacked(our_queen_sq, opp)) {
            int pt = piece_type((Piece)m.moved);

            if (pt == WQ) {
                if (!board.is_square_attacked(m.to, opp)) {
                    int escape_bonus = 2000;
                    int to_rank = rank_of(m.to);
                    bool back_rank_retreat = (mover == WHITE && to_rank == 0) ||
                                             (mover == BLACK && to_rank == 7);
                    if (back_rank_retreat) {
                        escape_bonus = 800;
                    }
                    score += escape_bonus;
                } else {
                    score += 500;
                }
                if (m.captured != EMPTY) {
                    score += 800;
                }
            } else {
                if (m.captured == EMPTY) {
                    score -= 1500;
                }
            }
        }

        // Queen moving to attacked square = SUICIDE
        int pt_queen = piece_type((Piece)m.moved);
        if (pt_queen == WQ && m.captured == EMPTY) {
            if (board.is_square_attacked(m.to, opp)) {
                score -= 2000;
            }
        }
    }

    // === ANY piece moving to attacked undefended square ===
    {
        int pt = piece_type((Piece)m.moved);
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;

        if (m.captured == EMPTY && board.is_square_attacked(m.to, opp)) {
            if (!board.is_square_attacked(m.to, mover)) {
                if (pt == WQ) {
                    score -= 2500;
                } else if (pt == WR) {
                    score -= 1000;
                } else if (pt == WB || pt == WN) {
                    score -= 600;
                }
            }
        }
    }

    // === Captures ===
    if (m.captured != EMPTY) {
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;

        int our_value = piece_value(m.moved);
        int their_value = piece_value(m.captured);
        bool is_defended = board.is_square_attacked(m.to, opp);

        int effective_our_value = (piece_type((Piece)m.moved) == WK) ? 0 : our_value;

        if (!is_defended) {
            score += 10 * their_value - effective_our_value;
        } else {
            if (piece_type((Piece)m.moved) == WK) {
                score += their_value;
            } else if (our_value <= their_value + 50) {
                score += their_value - our_value + 100;
            } else {
                int material_loss = our_value - their_value;
                score -= material_loss * 2;
            }
        }
    }
    if (m.promotion != EMPTY) {
        score += piece_value(m.promotion) + 800;
    }

    // === Castle ===
    if (m.is_castle) {
        score += 350;
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;
        bool queenside = (file_of(m.to) == 2);
        int files_to_check[3];
        if (queenside) { files_to_check[0]=0; files_to_check[1]=1; files_to_check[2]=2; }
        else           { files_to_check[0]=5; files_to_check[1]=6; files_to_check[2]=7; }
        for (int fc : files_to_check) {
            bool has_own_pawn = false;
            bool has_enemy_heavy = false;
            for (int r = 0; r < 8; ++r) {
                int p = pos.board[make_sq(fc, r)];
                if ((mover==WHITE && p==WP) || (mover==BLACK && p==BP)) has_own_pawn = true;
                bool enemy = (mover==WHITE) ? is_black((Piece)p) : is_white((Piece)p);
                if (enemy && p != EMPTY) {
                    int pt = piece_type((Piece)p);
                    if (pt == WR || pt == WQ) has_enemy_heavy = true;
                }
            }
            if (!has_own_pawn) {
                score -= 120;
                if (has_enemy_heavy) score -= 150;
            }
        }
        // Long diagonal bishop threat for O-O-O
        if (queenside) {
            int opp_bishop = (mover == WHITE) ? BB : WB;
            int opp_queen_p = (mover == WHITE) ? BQ : WQ;
            for (int i = 0; i < 8; ++i) {
                int sq = make_sq(i, i);
                int p = pos.board[sq];
                if (p == opp_bishop || p == opp_queen_p) {
                    score -= 150;
                    break;
                }
                if (p != EMPTY && sq != m.from) break;
            }
        }
    }

    // === Penalize king moves if castling available ===
    {
        int pt = piece_type((Piece)m.moved);
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        if (pt == WK && !m.is_castle) {
            bool can_castle_k = (mover == WHITE) ? (pos.castling_rights & 1) : (pos.castling_rights & 4);
            bool can_castle_q = (mover == WHITE) ? (pos.castling_rights & 2) : (pos.castling_rights & 8);
            if (can_castle_k || can_castle_q) {
                score -= 300;
            }
        }
    }

    if (is_reverse_of_last(board, m)) {
        score -= reverse_move_penalty(m);
    }
    if (is_repeat_piece_move(board, m)) {
        int pt = piece_type((Piece)m.moved);
        int penalty = 150;
        if (pt == WB) penalty = 200;
        if (pt == WN) penalty = 180;
        if (pt == WR) penalty = 220;
        score -= penalty;
    }

    // === Bonus for developing knights and bishops ===
    {
        int pt = piece_type((Piece)m.moved);
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        int to_rank = rank_of(m.to);
        int to_file = file_of(m.to);

        if (pos.fullmove_number <= 12 && m.captured == EMPTY) {
            if (pt == WN) {
                if (mover == WHITE && to_rank == 2) {
                    if (to_file == 2 || to_file == 5) score += 80;
                }
                if (mover == BLACK && to_rank == 5) {
                    if (to_file == 2 || to_file == 5) score += 80;
                }
                if (to_file >= 2 && to_file <= 5 && to_rank >= 2 && to_rank <= 5) {
                    score += 40;
                }
            }

            if (pt == WB) {
                if (to_file >= 1 && to_file <= 6 && to_rank >= 1 && to_rank <= 6) {
                    score += 50;
                }
                if ((to_file == 1 && to_rank == 1) || (to_file == 6 && to_rank == 1) ||
                    (to_file == 1 && to_rank == 6) || (to_file == 6 && to_rank == 6)) {
                    score += 30;
                }
                if ((to_file >= 2 && to_file <= 5) && (to_rank >= 2 && to_rank <= 5)) {
                    score += 40;
                }
            }

            if (pt == WP) {
                if (to_file == 3 || to_file == 4) {
                    if ((mover == WHITE && (to_rank == 2 || to_rank == 3)) ||
                        (mover == BLACK && (to_rank == 4 || to_rank == 5))) {
                        score += 60;
                    }
                }
            }
        }
    }

    // === Penalize pointless rook moves in opening ===
    int pt = piece_type((Piece)m.moved);
    if (pt == WR && m.captured == EMPTY && pos.fullmove_number <= 15) {
        int to_file = file_of(m.to);
        int to_rank = rank_of(m.to);
        if (to_rank == 0 || to_rank == 7) {
            if (to_file == 1 || to_file == 2 || to_file == 5 || to_file == 6) {
                score -= 200;
            }
            if (pos.fullmove_number <= 10) {
                score -= 150;
            }
        }
    }

    // === Discourage moving pieces into attacked squares ===
    if (m.moved != EMPTY && m.captured == EMPTY) {
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;
        if (board.is_square_attacked(m.to, opp)) {
            if (!board.is_square_attacked(m.to, mover)) {
                score -= piece_value(m.moved);
            } else {
                score -= piece_value(m.moved) / 4;
            }
        }
    }

    // === ANTI-GREED: grabbing distant pawns in opening ===
    if (pos.fullmove_number <= 12 && m.captured != EMPTY) {
        int pt2 = piece_type((Piece)m.moved);
        int cap_pt = piece_type((Piece)m.captured);

        if ((pt2 == WB || pt2 == WN) && cap_pt == WP) {
            int to_file = file_of(m.to);
            if (to_file == 0 || to_file == 7) {
                score -= 150;
            }
            if (pt2 == WB) {
                int to_rank = rank_of(m.to);
                if ((to_file == 0 && (to_rank == 1 || to_rank == 6)) ||
                    (to_file == 7 && (to_rank == 1 || to_rank == 6))) {
                    score -= 100;
                }
            }
        }
    }

    // === Penalize flank pawn moves in opening ===
    {
        int pt2 = piece_type((Piece)m.moved);
        if (pt2 == WP && m.captured == EMPTY && pos.fullmove_number <= 10) {
            int from_file = file_of(m.from);

            if (from_file == 0 || from_file == 7) {
                score -= 180;
            }
            if (from_file == 1) {
                int to_rank = rank_of(m.to);
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                if (mover == WHITE && to_rank == 3) score -= 120;
                else if (mover == BLACK && to_rank == 4) score -= 120;
            }
            if (from_file == 6 && pos.fullmove_number <= 8) {
                score -= 100;
            }
        }
    }

    // === Pawn moves in front of castled king ===
    {
        int pt2 = piece_type((Piece)m.moved);
        if (pt2 == WP && m.captured == EMPTY) {
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;
            int from_file = file_of(m.from);

            int our_king = (mover == WHITE) ? WK : BK;
            int king_sq = -1;
            for (int sq = 0; sq < 64; ++sq) {
                if (pos.board[sq] == our_king) {
                    king_sq = sq;
                    break;
                }
            }

            if (king_sq != -1) {
                int king_file = file_of(king_sq);
                int king_rank = rank_of(king_sq);

                bool kingside_castled = (mover == WHITE)
                    ? (king_file >= 6 && king_rank == 0)
                    : (king_file >= 6 && king_rank == 7);

                bool queenside_castled = (mover == WHITE)
                    ? (king_file <= 2 && king_rank == 0)
                    : (king_file <= 2 && king_rank == 7);

                int opp_queen = (opp == WHITE) ? WQ : BQ;
                bool enemy_queen_active = false;
                for (int sq = 0; sq < 64; ++sq) {
                    if (pos.board[sq] == opp_queen) {
                        int qr = rank_of(sq);
                        if ((opp == WHITE && qr >= 2) || (opp == BLACK && qr <= 5)) {
                            enemy_queen_active = true;
                        }
                        break;
                    }
                }

                if (kingside_castled && enemy_queen_active) {
                    if (from_file == 6 || from_file == 7) score -= 300;
                    if (from_file == 5) score -= 200;
                }

                if (queenside_castled && enemy_queen_active) {
                    if (from_file <= 2) score -= 250;
                }
            }
        }
    }

    // === Queen grabbing flank pawns is usually a TRAP ===
    {
        int pt2 = piece_type((Piece)m.moved);
        if (pt2 == WQ && m.captured != EMPTY) {
            int cap_pt = piece_type((Piece)m.captured);
            if (cap_pt == WP) {
                int to_file = file_of(m.to);
                int to_rank = rank_of(m.to);
                Side mover = (m.moved <= WK) ? WHITE : BLACK;

                if (to_file == 0 || to_file == 1) {
                    score -= 200;
                    if ((mover == WHITE && to_rank >= 5) || (mover == BLACK && to_rank <= 2)) {
                        score -= 150;
                    }
                }
                if (to_file == 6 || to_file == 7) {
                    score -= 200;
                    if ((mover == WHITE && to_rank >= 5) || (mover == BLACK && to_rank <= 2)) {
                        score -= 150;
                    }
                }

                int opp_bishop = (mover == WHITE) ? BB : WB;
                for (int sq = 0; sq < 64; ++sq) {
                    if (pos.board[sq] == opp_bishop) {
                        int bf = file_of(sq);
                        int br = rank_of(sq);
                        int tf = to_file;
                        int tr = to_rank;
                        if (abs(bf - tf) == abs(br - tr) && abs(bf - tf) <= 3) {
                            score -= 250;
                        }
                    }
                }
            }
        }
    }

    // === Never trade queen for minor piece ===
    {
        int pt2 = piece_type((Piece)m.moved);
        if (pt2 == WQ && m.captured != EMPTY) {
            int cap_pt = piece_type((Piece)m.captured);
            if (cap_pt == WB || cap_pt == WN || cap_pt == WP) {
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                Side opp = (mover == WHITE) ? BLACK : WHITE;
                if (board.is_square_attacked(m.to, opp)) {
                    int captured_value = piece_value(m.captured);
                    int queen_value = 900;
                    int net_loss = queen_value - captured_value;
                    score -= (10 * captured_value);
                    score -= net_loss * 3;
                }
            }
            if (cap_pt == WR) {
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                Side opp = (mover == WHITE) ? BLACK : WHITE;
                if (board.is_square_attacked(m.to, opp)) {
                    score -= 800;
                }
            }
        }

        // === Never move king to center of board ===
        if (pt2 == WK) {
            int to_rank = rank_of(m.to);
            int to_file = file_of(m.to);
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;

            if (board.is_square_attacked(m.from, opp)) {
                if ((mover == WHITE && to_rank >= 1) || (mover == BLACK && to_rank <= 6)) {
                    score -= 600;
                }
                if (to_file >= 3 && to_file <= 4) {
                    score -= 400;
                }
            }

            if (to_rank >= 2 && to_rank <= 5) {
                score -= 500;
                if (to_file >= 2 && to_file <= 5) {
                    score -= 300;
                }
            }
            if (!m.is_castle && pos.fullmove_number <= 15) {
                int from_rank = rank_of(m.from);
                if ((from_rank == 0 && to_rank > 0) || (from_rank == 7 && to_rank < 7)) {
                    score -= 400;
                }
            }

            if (board.is_square_attacked(m.to, opp)) {
                score -= 300;
            }

            for (int r = 0; r < 8; ++r) {
                int sq = make_sq(to_file, r);
                int p = pos.board[sq];
                if (p == EMPTY) continue;
                bool enemy_piece = (mover == WHITE) ? is_black((Piece)p) : is_white((Piece)p);
                if (enemy_piece) {
                    int pt3 = piece_type((Piece)p);
                    if (pt3 == WR || pt3 == WQ) {
                        score -= 200;
                    }
                }
            }
        }
    }

    return score;
}

// ========================
// Mate Detection
// ========================

bool Searcher::has_mate_in_one(Board &board) {
    auto moves = generate_legal_moves(board);
    for (auto &m : moves) {
        Undo u = board.make_move(m);
        if (!board.in_check(board.pos.side_to_move)) {
            board.unmake_move(m, u);
            continue;
        }
        auto replies = generate_legal_moves(board);
        bool mate = replies.empty();
        board.unmake_move(m, u);
        if (mate) return true;
    }
    return false;
}

int Searcher::mate_search(Board &board, int depth, bool maximizing) {
    mate_search_nodes++;
    if (mate_search_nodes > MATE_SEARCH_NODE_LIMIT) return 0;
    if (time_up()) return 0;
    if (depth <= 0) return 0;

    auto moves = generate_legal_moves(board);

    if (moves.empty()) {
        if (board.in_check(board.pos.side_to_move)) {
            return maximizing ? -(MATE_SCORE - (10 - depth)) : (MATE_SCORE - (10 - depth));
        }
        return 0;
    }

    if (maximizing) {
        int best = 0;
        for (auto &m : moves) {
            Undo u = board.make_move(m);
            bool gives_check = board.in_check(board.pos.side_to_move);

            if (gives_check) {
                int score = mate_search(board, depth - 1, false);
                if (score > best) best = score;
                if (best >= MATE_SCORE - 20) {
                    board.unmake_move(m, u);
                    return best;
                }
            }
            board.unmake_move(m, u);
        }

        if (depth >= 4 && best == 0) {
            for (auto &m : moves) {
                if (m.captured != EMPTY) {
                    Undo u = board.make_move(m);
                    int score = mate_search(board, depth - 1, false);
                    if (score > best) best = score;
                    board.unmake_move(m, u);
                    if (best >= MATE_SCORE - 20) return best;
                }
            }
        }

        return best;
    } else {
        int worst = MATE_SCORE;
        for (auto &m : moves) {
            Undo u = board.make_move(m);
            int score = mate_search(board, depth - 1, true);
            board.unmake_move(m, u);

            if (score < worst) worst = score;
            if (worst == 0) return 0;
        }
        return worst;
    }
}

int Searcher::has_mate_in_n(Board &board, int depth) {
    mate_search_nodes = 0;
    return mate_search(board, depth * 2, true);
}

int Searcher::opponent_has_mate_in_n(Board &board, int depth) {
    mate_search_nodes = 0;
    return mate_search(board, depth * 2, true);
}

bool Searcher::opponent_has_mate_in_one(Board &board) {
    return has_mate_in_one(board);
}

bool Searcher::can_force_mate(Board &board, int plies) {
    if (plies <= 0) return false;
    if (time_up()) return false;
    if (++mate_probe_nodes > MATE_PROBE_LIMIT) return false;

    auto moves = generate_legal_moves(board);
    if (moves.empty()) return false;

    for (auto &m : moves) {
        Undo u = board.make_move(m);
        bool mate_found = false;
        auto replies = generate_legal_moves(board);
        if (replies.empty()) {
            if (board.in_check(board.pos.side_to_move)) {
                mate_found = true;
            }
        } else if (plies >= 2) {
            mate_found = true;
            for (auto &r : replies) {
                Undo ur = board.make_move(r);
                bool child = can_force_mate(board, plies - 2);
                board.unmake_move(r, ur);
                if (!child) {
                    mate_found = false;
                    break;
                }
            }
        }
        board.unmake_move(m, u);
        if (mate_found) return true;
    }
    return false;
}

bool Searcher::is_mate_trap(Board &board, const Move &m, int plies) {
    mate_probe_nodes = 0;
    Undo u = board.make_move(m);
    bool trap = can_force_mate(board, plies);
    board.unmake_move(m, u);
    return trap;
}

// ========================
// Alpha-Beta Search
// ========================

int Searcher::alpha_beta_root(Board &board, int depth, int alpha, int beta, Move &best,
                              vector<pair<Move, int>> &root_scores, const Move &prev_best) {
    node_count++;
    if (time_up()) return 0;

    if (board.pos.halfmove_clock >= 100 || board.is_repetition() || board.is_insufficient_material()) {
        int draw_score = evaluate(board) / 10;
        draw_score = clamp(draw_score, -20, 20);
        return draw_score;
    }

    bool in_check_now = board.in_check(board.pos.side_to_move);
    auto moves = generate_legal_moves(board);
    if (moves.empty()) {
        if (in_check_now) return -MATE_SCORE + (100 - depth);
        return 0;
    }

    sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b) {
        return move_score(board, a) > move_score(board, b);
    });

    // PV move ordering
    if (prev_best.from != -1) {
        for (size_t i = 0; i < moves.size(); ++i) {
            if (moves[i].from == prev_best.from && moves[i].to == prev_best.to &&
                moves[i].promotion == prev_best.promotion) {
                if (i > 0) {
                    Move tmp = moves[i];
                    moves.erase(moves.begin() + i);
                    moves.insert(moves.begin(), tmp);
                }
                break;
            }
        }
    }

    int best_score = -INF;
    root_scores.clear();

    bool collect_all = opening_variety && board.pos.fullmove_number <= 8;

    for (auto &m : moves) {
        Undo u = board.make_move(m);

        if (has_mate_in_one(board)) {
            board.unmake_move(m, u);
            continue;
        }

        bool gives_check = board.in_check(board.pos.side_to_move);
        int extension = 0;
        if (gives_check) {
            bool checker_attacked = board.is_square_attacked(m.to, board.pos.side_to_move);
            bool checker_defended = board.is_square_attacked(m.to, opposite(board.pos.side_to_move));
            if (!checker_attacked || checker_defended) {
                extension = 1;
            }
        }

        // Blunder filter
        if (!gives_check) {
            int pt_moved = piece_type((Piece)m.moved);
            int see = see_score(board, m);
            if ((pt_moved == WQ && see < -200) ||
                (pt_moved == WR && see < -200) ||
                ((pt_moved == WB || pt_moved == WN) && see < -200)) {
                board.unmake_move(m, u);
                continue;
            }
        }

        Move child_best;
        int search_alpha = collect_all ? max(-INF, best_score - 50) : alpha;
        int score = -alpha_beta(board, depth - 1 + extension, -beta, -search_alpha, child_best);

        board.unmake_move(m, u);

        // Penalize queen grabbing pawns deep in opponent territory
        {
            int pt = piece_type((Piece)m.moved);
            if (pt == WQ && m.captured != EMPTY && piece_type((Piece)m.captured) == WP) {
                int to_rank = rank_of(m.to);
                bool queen_is_white = is_white((Piece)m.moved);
                bool is_deep_invasion = queen_is_white ? (to_rank >= 6) : (to_rank <= 1);
                if (is_deep_invasion) {
                    int undeveloped = 0;
                    if (queen_is_white) {
                        if (board.pos.board[make_sq(1,0)] == WN) undeveloped++;
                        if (board.pos.board[make_sq(6,0)] == WN) undeveloped++;
                        if (board.pos.board[make_sq(2,0)] == WB) undeveloped++;
                        if (board.pos.board[make_sq(5,0)] == WB) undeveloped++;
                    } else {
                        if (board.pos.board[make_sq(1,7)] == BN) undeveloped++;
                        if (board.pos.board[make_sq(6,7)] == BN) undeveloped++;
                        if (board.pos.board[make_sq(2,7)] == BB) undeveloped++;
                        if (board.pos.board[make_sq(5,7)] == BB) undeveloped++;
                    }
                    score -= 150 + undeveloped * 100;
                }
            }
        }

        root_scores.push_back({m, score});

        if (stop_search) return 0;
        if (score > best_score) {
            best_score = score;
            best = m;
        }
        if (score > alpha) alpha = score;
        if (!collect_all && alpha >= beta) break;
    }

    return best_score;
}

int Searcher::alpha_beta(Board &board, int depth, int alpha, int beta, Move &best) {
    node_count++;
    if (time_up()) return 0;
    if (depth == 0) return quiescence(board, alpha, beta);

    if (board.pos.halfmove_clock >= 100 || board.is_repetition() || board.is_insufficient_material()) {
        int draw_score = evaluate(board) / 10;
        draw_score = clamp(draw_score, -20, 20);
        return draw_score;
    }

    bool in_check_now = board.in_check(board.pos.side_to_move);

    auto moves = generate_legal_moves(board);
    if (moves.empty()) {
        if (in_check_now) return -MATE_SCORE + (100 - depth);
        return 0;
    }

    sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b) {
        return move_score(board, a) > move_score(board, b);
    });

    int best_score = -INF;

    for (auto &m : moves) {
        bool is_reverse = is_reverse_of_last(board, m);
        bool is_repeat = is_repeat_piece_move(board, m);

        Undo u = board.make_move(m);

        // Check if allows mate in 1 (only at depth >= 3)
        if (depth >= 3 && has_mate_in_one(board)) {
            board.unmake_move(m, u);
            continue;
        }

        bool gives_check = board.in_check(board.pos.side_to_move);
        int extension = 0;
        if (gives_check) {
            bool checker_attacked = board.is_square_attacked(m.to, board.pos.side_to_move);
            bool checker_defended = board.is_square_attacked(m.to, opposite(board.pos.side_to_move));
            if (!checker_attacked || checker_defended) {
                extension = 1;
            }
        }

        // Blunder filter
        if (!gives_check) {
            int pt_moved = piece_type((Piece)m.moved);
            int see = see_score(board, m);
            if ((pt_moved == WQ && see < -200) ||
                (pt_moved == WR && see < -200) ||
                ((pt_moved == WB || pt_moved == WN) && see < -200)) {
                board.unmake_move(m, u);
                continue;
            }
        }

        Move child_best;
        int score = -alpha_beta(board, depth - 1 + extension, -beta, -alpha, child_best);

        board.unmake_move(m, u);

        // Post-unmake penalties
        if (is_reverse) {
            score -= reverse_move_penalty(m);
        }
        if (is_repeat) {
            score -= 120;
        }

        // Penalize pointless rook moves
        int pt = piece_type((Piece)m.moved);
        if (pt == WR && m.captured == EMPTY && board.pos.fullmove_number <= 15) {
            int to_file = file_of(m.to);
            int to_rank = rank_of(m.to);
            if (to_rank == 0 || to_rank == 7) {
                if (to_file == 1 || to_file == 2 || to_file == 5 || to_file == 6) {
                    score -= 200;
                }
                if (board.pos.fullmove_number <= 10) {
                    score -= 150;
                }
            }
        }

        // Penalize queen pawn grabbing deep
        if (pt == WQ && m.captured != EMPTY && piece_type((Piece)m.captured) == WP) {
            int to_rank = rank_of(m.to);
            bool queen_is_white = is_white((Piece)m.moved);
            bool is_deep_invasion = queen_is_white ? (to_rank >= 6) : (to_rank <= 1);
            if (is_deep_invasion) {
                int undeveloped = 0;
                if (queen_is_white) {
                    if (board.pos.board[make_sq(1,0)] == WN) undeveloped++;
                    if (board.pos.board[make_sq(6,0)] == WN) undeveloped++;
                    if (board.pos.board[make_sq(2,0)] == WB) undeveloped++;
                    if (board.pos.board[make_sq(5,0)] == WB) undeveloped++;
                } else {
                    if (board.pos.board[make_sq(1,7)] == BN) undeveloped++;
                    if (board.pos.board[make_sq(6,7)] == BN) undeveloped++;
                    if (board.pos.board[make_sq(2,7)] == BB) undeveloped++;
                    if (board.pos.board[make_sq(5,7)] == BB) undeveloped++;
                }
                score -= 150 + undeveloped * 100;
            }
        }

        // Penalize king to center in middlegame
        if (pt == WK) {
            bool has_queen = false;
            for (int sq = 0; sq < 64; ++sq) {
                if (board.pos.board[sq] == WQ || board.pos.board[sq] == BQ) {
                    has_queen = true;
                    break;
                }
            }
            if (has_queen) {
                int cap_value = piece_type((Piece)m.captured);
                bool captures_major = (cap_value == WQ || cap_value == WR);
                if (!captures_major) {
                    int to_rank = rank_of(m.to);
                    int to_file = file_of(m.to);
                    if (to_rank >= 2 && to_rank <= 5) {
                        score -= 500;
                        if (to_file >= 2 && to_file <= 5) {
                            score -= 300;
                        }
                    }
                }
            }
        }

        // Penalize trading queen for minor piece
        if (pt == WQ && m.captured != EMPTY) {
            int cap_pt = piece_type((Piece)m.captured);
            if (cap_pt == WB || cap_pt == WN || cap_pt == WP) {
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                Side opp = (mover == WHITE) ? BLACK : WHITE;
                if (board.is_square_attacked(m.to, opp)) {
                    score -= 600;
                }
            }
        }

        if (stop_search) return 0;
        if (score > best_score) {
            best_score = score;
            best = m;
        }
        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }

    return best_score;
}

int Searcher::quiescence(Board &board, int alpha, int beta) {
    if (time_up()) return 0;

    bool in_check_now = board.in_check(board.pos.side_to_move);

    if (in_check_now) {
        auto moves = generate_legal_moves(board);
        if (moves.empty()) {
            return -MATE_SCORE;
        }
        int best_score = -INF;
        for (auto &m : moves) {
            Undo u = board.make_move(m);
            int score = -quiescence(board, -beta, -alpha);
            board.unmake_move(m, u);
            if (stop_search) return 0;
            if (score > best_score) best_score = score;
            if (score > alpha) alpha = score;
            if (alpha >= beta) return beta;
        }
        return best_score;
    }

    int stand = evaluate(board);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    int delta = 900;
    if (stand + delta < alpha) return stand;

    vector<Move> moves;
    generate_pseudo_moves(board, moves);

    vector<Move> captures;
    for (auto &m : moves) {
        if (m.captured != EMPTY || m.is_en_passant || m.promotion != EMPTY) {
            captures.push_back(m);
        }
    }

    sort(captures.begin(), captures.end(), [&](const Move &a, const Move &b) {
        return piece_value(a.captured) - piece_value(a.moved)/10 >
               piece_value(b.captured) - piece_value(b.moved)/10;
    });

    for (auto &m : captures) {
        Undo u = board.make_move(m);
        if (board.in_check(opposite(board.pos.side_to_move))) {
            board.unmake_move(m, u);
            continue;
        }
        int score = -quiescence(board, -beta, -alpha);
        board.unmake_move(m, u);
        if (stop_search) return 0;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// ========================
// Main Search Entry Point
// ========================

Move Searcher::search_bestmove(Board &board, int max_depth, int time_limit_ms) {
    stop_search = false;
    search_start = chrono::steady_clock::now();
    time_limit = time_limit_ms;
    Move best;
    int best_score = -INF;
    int depth_reached = 0;

    // Count heavy pieces
    int heavy_pieces = 0;
    for (int sq = 0; sq < 64; ++sq) {
        int p = board.pos.board[sq];
        if (p == WQ || p == BQ || p == WR || p == BR) heavy_pieces++;
    }

    // === QUICK CHECK: Forced mate in 3 ===
    int our_mate = 0;
    if (heavy_pieces > 0) {
        our_mate = has_mate_in_n(board, 3);
    }
    if (our_mate >= MATE_SCORE - 20) {
        auto moves = generate_legal_moves(board);

        Move best_mate_move;
        int best_mate_score = 0;

        for (auto &m : moves) {
            Undo u = board.make_move(m);
            bool gives_check = board.in_check(board.pos.side_to_move);

            if (gives_check) {
                auto opp_moves = generate_legal_moves(board);
                if (opp_moves.empty()) {
                    board.unmake_move(m, u);
                    return m;
                }

                mate_search_nodes = 0;
                int score = mate_search(board, 5, false);
                if (score > best_mate_score) {
                    best_mate_score = score;
                    best_mate_move = m;
                }
            }
            board.unmake_move(m, u);
        }

        if (best_mate_score >= MATE_SCORE - 20) {
            return best_mate_move;
        }
    }

    // === QUICK CHECK: Obvious promotion ===
    {
        auto moves = generate_legal_moves(board);
        Move best_promo;
        for (auto &m : moves) {
            if (m.promotion == WQ || m.promotion == BQ) {
                Undo u = board.make_move(m);
                bool queen_safe = !board.is_square_attacked(m.to, board.pos.side_to_move);
                board.unmake_move(m, u);
                if (queen_safe) {
                    best_promo = m;
                    break;
                }
            }
        }
        if (best_promo.from != -1) {
            return best_promo;
        }
    }

    // Main iterative deepening search
    node_count = 0;
    Move prev_best;
    vector<pair<Move, int>> root_scores;
    vector<pair<Move, int>> last_complete_root_scores;
    for (int depth = 1; depth <= max_depth; ++depth) {
        Move curr_best;
        root_scores.clear();
        int score = alpha_beta_root(board, depth, -INF, INF, curr_best, root_scores, prev_best);
        if (stop_search) {
            if (prev_best.from != -1) {
                best = prev_best;
            } else if (curr_best.from != -1) {
                best = curr_best;
            }
            break;
        }
        best = curr_best;
        prev_best = curr_best;
        best_score = score;
        depth_reached = depth;
        last_complete_root_scores = root_scores;
        cout << "info depth " << depth << " score cp " << score
             << " nodes " << node_count
             << " pv " << move_to_uci(curr_best) << endl;
    }

    // === OPENING VARIETY ===
    if (opening_variety && board.pos.fullmove_number <= 8 && !last_complete_root_scores.empty()) {
        sort(last_complete_root_scores.begin(), last_complete_root_scores.end(),
             [](auto &a, auto &b) { return a.second > b.second; });
        int top_score = last_complete_root_scores[0].second;
        int threshold = 25;
        vector<pair<Move, int>> good_moves;
        for (auto &c : last_complete_root_scores) {
            if (top_score - c.second <= threshold) {
                int pt = piece_type((Piece)c.first.moved);
                int to_file = file_of(c.first.to);
                if (pt == WN && (to_file == 0 || to_file == 7)) continue;
                if (pt == WP) {
                    int from_file = file_of(c.first.from);
                    if (from_file == 0 || from_file == 7) continue;
                }
                if (pt == WR) continue;
                good_moves.push_back(c);
            }
        }
        if (good_moves.size() > 1) {
            uniform_int_distribution<int> dist(0, (int)good_moves.size() - 1);
            int pick = dist(opening_rng);
            best = good_moves[pick].first;
            best_score = good_moves[pick].second;
        }
    }

    // === SAFETY CHECK: mate in 1/2 ===
    if (best.from != -1 && !stop_search) {
        Undo u = board.make_move(best);
        bool allows_mate1 = opponent_has_mate_in_one(board);
        int opp_mate = 0;
        if (!allows_mate1) {
            opp_mate = opponent_has_mate_in_n(board, 2);
        }

        if (allows_mate1 || opp_mate >= MATE_SCORE - 20) {
            board.unmake_move(best, u);
            cerr << "info string WARNING: Best move allows mate, finding alternative" << endl;

            auto moves = generate_legal_moves(board);
            Move safe_best;
            int safe_score = -INF;

            for (auto &m : moves) {
                Undo u2 = board.make_move(m);
                bool m_allows_mate1 = opponent_has_mate_in_one(board);
                int m_opp_mate = m_allows_mate1 ? MATE_SCORE : opponent_has_mate_in_n(board, 2);

                int score;
                if (m_allows_mate1 || m_opp_mate >= MATE_SCORE - 20) {
                    score = -MATE_SCORE;
                } else {
                    Move child_best;
                    score = -alpha_beta(board, 3, -INF, INF, child_best);
                }
                board.unmake_move(m, u2);

                if (score > safe_score) {
                    safe_score = score;
                    safe_best = m;
                }
            }

            if (safe_best.from != -1) {
                best = safe_best;
            }
        } else {
            board.unmake_move(best, u);
        }
    }

    if (best.from != -1 && !stop_search) {
        if (is_mate_trap(board, best, MATE_TRAP_PLIES)) {
            auto moves = generate_legal_moves(board);
            Move alt_best = best;
            int alt_score = -INF;
            int alt_depth = max(1, min(3, depth_reached - 1));
            for (auto &m : moves) {
                if (time_up()) break;
                if (is_mate_trap(board, m, MATE_TRAP_PLIES)) continue;
                Undo u = board.make_move(m);
                Move child_best;
                int score = -alpha_beta(board, alt_depth, -INF, INF, child_best);
                board.unmake_move(m, u);
                if (score > alt_score) {
                    alt_score = score;
                    alt_best = m;
                }
            }
            if (alt_score > -INF / 2) {
                best = alt_best;
            }
        }
    }

    if (best.from == -1) {
        auto moves = generate_legal_moves(board);
        if (!moves.empty()) {
            best = moves[0];
        }
    }
    return best;
}

} // namespace chess
