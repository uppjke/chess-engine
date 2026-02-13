#include "search.h"
#include "position.h"
#include "movegen.h"
#include "evaluation.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace std;

namespace chess {

// ========================
// Init / Reset
// ========================

Searcher::Searcher() {
    tt.resize(TT_SIZE);
    memset(history, 0, sizeof(history));
    memset(killers, 0, sizeof(killers));
    opening_rng.seed(chrono::steady_clock::now().time_since_epoch().count());
}

void Searcher::new_game() {
    fill(tt.begin(), tt.end(), TTEntry{});
    memset(history, 0, sizeof(history));
    memset(killers, 0, sizeof(killers));
    opening_rng.seed(chrono::steady_clock::now().time_since_epoch().count());
}

// ========================
// TT score adjustment for mate scores
// ========================

int Searcher::score_to_tt(int score, int ply) const {
    if (score >= MATE_SCORE - 200) return score + ply;
    if (score <= -MATE_SCORE + 200) return score - ply;
    return score;
}

int Searcher::score_from_tt(int score, int ply) const {
    if (score >= MATE_SCORE - 200) return score - ply;
    if (score <= -MATE_SCORE + 200) return score + ply;
    return score;
}

// ========================
// Time Management
// ========================

bool Searcher::time_up() {
    if (stop_search) return true;
    // Only check the clock every 4096 nodes
    if ((node_count & 4095) != 0) return false;
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
// Simple Move Score (inner nodes — fast)
// ========================

int Searcher::simple_move_score(const Board &board, const Move &m,
                                const Move &tt_move, int ply) const {
    // TT best move
    if (tt_move.from != -1 && m.from == tt_move.from && m.to == tt_move.to
        && m.promotion == tt_move.promotion) {
        return 10000000;
    }

    // Promotions
    if (m.promotion != EMPTY) {
        return 6000000 + piece_value(m.promotion);
    }

    // Captures — MVV-LVA
    if (m.captured != EMPTY) {
        return 5000000 + piece_value(m.captured) * 10 - piece_value(m.moved);
    }

    // Killer moves
    if (ply < MAX_PLY) {
        if (killers[ply][0].from == m.from && killers[ply][0].to == m.to) return 4000000;
        if (killers[ply][1].from == m.from && killers[ply][1].to == m.to) return 3900000;
    }

    // History heuristic
    int side = (int)board.pos.side_to_move;
    return history[side][m.from][m.to];
}

// ========================
// Complex Move Score (root only — thorough)
// ========================

int Searcher::move_score(const Board &board, const Move &m) const {
    int score = 0;
    const auto &pos = board.pos;

    // === Capture enemy QUEEN ===
    {
        int cap_pt = piece_type((Piece)m.captured);
        if (cap_pt == WQ) {
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;
            int our_value = piece_value(m.moved);
            if (!board.is_square_attacked(m.to, opp)) {
                score += 5000;
            } else {
                if (our_value < 900) score += 3000;
            }
        }
    }

    // === Check if our queen is under attack ===
    {
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;
        int our_queen = (mover == WHITE) ? WQ : BQ;
        int our_queen_sq = -1;
        for (int sq = 0; sq < 64; ++sq) {
            if (pos.board[sq] == our_queen) { our_queen_sq = sq; break; }
        }
        if (our_queen_sq != -1 && board.is_square_attacked(our_queen_sq, opp)) {
            int pt = piece_type((Piece)m.moved);
            if (pt == WQ) {
                if (!board.is_square_attacked(m.to, opp)) {
                    int escape_bonus = 2000;
                    int to_rank = rank_of(m.to);
                    bool back_rank = (mover == WHITE && to_rank == 0) || (mover == BLACK && to_rank == 7);
                    if (back_rank) escape_bonus = 800;
                    score += escape_bonus;
                } else {
                    score += 500;
                }
                if (m.captured != EMPTY) score += 800;
            } else {
                if (m.captured == EMPTY) score -= 1500;
            }
        }
        // Queen moving to attacked square
        int pt_q = piece_type((Piece)m.moved);
        if (pt_q == WQ && m.captured == EMPTY && board.is_square_attacked(m.to, opp)) {
            score -= 2000;
        }
    }

    // === Piece moving to attacked undefended square ===
    {
        int pt = piece_type((Piece)m.moved);
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;
        if (m.captured == EMPTY && board.is_square_attacked(m.to, opp)) {
            if (!board.is_square_attacked(m.to, mover)) {
                if (pt == WQ) score -= 2500;
                else if (pt == WR) score -= 1000;
                else if (pt == WB || pt == WN) score -= 600;
            }
        }
    }

    // === Captures MVV-LVA ===
    if (m.captured != EMPTY) {
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;
        int our_value = piece_value(m.moved);
        int their_value = piece_value(m.captured);
        bool is_defended = board.is_square_attacked(m.to, opp);
        int eff = (piece_type((Piece)m.moved) == WK) ? 0 : our_value;
        if (!is_defended) {
            score += 10 * their_value - eff;
        } else {
            if (piece_type((Piece)m.moved) == WK) score += their_value;
            else if (our_value <= their_value + 50) score += their_value - our_value + 100;
            else score -= (our_value - their_value) * 2;
        }
    }
    if (m.promotion != EMPTY) score += piece_value(m.promotion) + 800;

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
            bool has_own_pawn = false, has_enemy_heavy = false;
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
        if (queenside) {
            int opp_bishop = (mover == WHITE) ? BB : WB;
            int opp_queen_p = (mover == WHITE) ? BQ : WQ;
            for (int i = 0; i < 8; ++i) {
                int sq = make_sq(i, i);
                int p = pos.board[sq];
                if (p == opp_bishop || p == opp_queen_p) { score -= 150; break; }
                if (p != EMPTY && sq != m.from) break;
            }
        }
    }

    // === Penalize king moves if castling available ===
    {
        int pt = piece_type((Piece)m.moved);
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        if (pt == WK && !m.is_castle) {
            bool can_k = (mover == WHITE) ? (pos.castling_rights & 1) : (pos.castling_rights & 4);
            bool can_q = (mover == WHITE) ? (pos.castling_rights & 2) : (pos.castling_rights & 8);
            if (can_k || can_q) score -= 300;
        }
    }

    if (is_reverse_of_last(board, m)) score -= reverse_move_penalty(m);
    if (is_repeat_piece_move(board, m)) {
        int pt = piece_type((Piece)m.moved);
        int penalty = 150;
        if (pt == WB) penalty = 200;
        if (pt == WN) penalty = 180;
        if (pt == WR) penalty = 220;
        score -= penalty;
    }

    // === Development bonuses ===
    {
        int pt = piece_type((Piece)m.moved);
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        int to_rank = rank_of(m.to);
        int to_file = file_of(m.to);

        if (pos.fullmove_number <= 12 && m.captured == EMPTY) {
            if (pt == WN) {
                if (mover == WHITE && to_rank == 2 && (to_file == 2 || to_file == 5)) score += 80;
                if (mover == BLACK && to_rank == 5 && (to_file == 2 || to_file == 5)) score += 80;
                if (to_file >= 2 && to_file <= 5 && to_rank >= 2 && to_rank <= 5) score += 40;
            }
            if (pt == WB) {
                if (to_file >= 1 && to_file <= 6 && to_rank >= 1 && to_rank <= 6) score += 50;
                if ((to_file >= 2 && to_file <= 5) && (to_rank >= 2 && to_rank <= 5)) score += 40;
            }
            if (pt == WP && (to_file == 3 || to_file == 4)) {
                if ((mover == WHITE && (to_rank == 2 || to_rank == 3)) ||
                    (mover == BLACK && (to_rank == 4 || to_rank == 5))) score += 60;
            }
        }
    }

    // === Penalize pointless rook moves in opening ===
    {
        int pt = piece_type((Piece)m.moved);
        if (pt == WR && m.captured == EMPTY && pos.fullmove_number <= 15) {
            int to_file = file_of(m.to);
            int to_rank = rank_of(m.to);
            if (to_rank == 0 || to_rank == 7) {
                if (to_file == 1 || to_file == 2 || to_file == 5 || to_file == 6) score -= 200;
                if (pos.fullmove_number <= 10) score -= 150;
            }
        }
    }

    // === Penalize flank pawn moves in opening ===
    {
        int pt = piece_type((Piece)m.moved);
        if (pt == WP && m.captured == EMPTY && pos.fullmove_number <= 10) {
            int from_file = file_of(m.from);
            if (from_file == 0 || from_file == 7) score -= 180;
            if (from_file == 1) {
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                int to_rank = rank_of(m.to);
                if (mover == WHITE && to_rank == 3) score -= 120;
                else if (mover == BLACK && to_rank == 4) score -= 120;
            }
            if (from_file == 6 && pos.fullmove_number <= 8) score -= 100;
        }
    }

    // === Never trade queen for minor piece ===
    {
        int pt = piece_type((Piece)m.moved);
        if (pt == WQ && m.captured != EMPTY) {
            int cap_pt = piece_type((Piece)m.captured);
            if (cap_pt == WB || cap_pt == WN || cap_pt == WP) {
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                Side opp = (mover == WHITE) ? BLACK : WHITE;
                if (board.is_square_attacked(m.to, opp)) {
                    score -= 10 * piece_value(m.captured);
                    score -= (900 - piece_value(m.captured)) * 3;
                }
            }
            if (cap_pt == WR) {
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                Side opp = (mover == WHITE) ? BLACK : WHITE;
                if (board.is_square_attacked(m.to, opp)) score -= 800;
            }
        }
        // king to center
        if (pt == WK) {
            int to_rank = rank_of(m.to);
            int to_file = file_of(m.to);
            if (to_rank >= 2 && to_rank <= 5) {
                score -= 500;
                if (to_file >= 2 && to_file <= 5) score -= 300;
            }
            if (!m.is_castle && pos.fullmove_number <= 15) {
                int from_rank = rank_of(m.from);
                if ((from_rank == 0 && to_rank > 0) || (from_rank == 7 && to_rank < 7)) score -= 400;
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
    // Direct time check (bypass throttled time_up)
    if ((mate_search_nodes & 255) == 0) {
        auto now = chrono::steady_clock::now();
        int elapsed = (int)chrono::duration_cast<chrono::milliseconds>(now - search_start).count();
        if (elapsed >= time_limit) { stop_search = true; return 0; }
    }
    if (stop_search) return 0;
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
                if (best >= MATE_SCORE - 20) { board.unmake_move(m, u); return best; }
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
    if (++mate_probe_nodes > MATE_PROBE_LIMIT) return false;
    // Direct time check (bypass throttled time_up)
    if ((mate_probe_nodes & 255) == 0) {
        auto now = chrono::steady_clock::now();
        int elapsed = (int)chrono::duration_cast<chrono::milliseconds>(now - search_start).count();
        if (elapsed >= time_limit) { stop_search = true; return false; }
    }
    if (stop_search) return false;

    auto moves = generate_legal_moves(board);
    if (moves.empty()) return false;

    for (auto &m : moves) {
        Undo u = board.make_move(m);
        bool mate_found = false;
        auto replies = generate_legal_moves(board);
        if (replies.empty()) {
            if (board.in_check(board.pos.side_to_move)) mate_found = true;
        } else if (plies >= 2) {
            mate_found = true;
            for (auto &r : replies) {
                Undo ur = board.make_move(r);
                bool child = can_force_mate(board, plies - 2);
                board.unmake_move(r, ur);
                if (!child) { mate_found = false; break; }
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
// Quiescence Search
// ========================

int Searcher::quiescence(Board &board, int alpha, int beta) {
    node_count++;
    if (time_up()) return 0;

    bool in_check_now = board.in_check(board.pos.side_to_move);

    if (in_check_now) {
        auto moves = generate_legal_moves(board);
        if (moves.empty()) return -MATE_SCORE;
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

    // Delta pruning
    if (stand + 900 < alpha) return stand;

    vector<Move> moves;
    generate_pseudo_moves(board, moves);

    vector<Move> captures;
    for (auto &m : moves) {
        if (m.captured != EMPTY || m.is_en_passant || m.promotion != EMPTY)
            captures.push_back(m);
    }

    sort(captures.begin(), captures.end(), [](const Move &a, const Move &b) {
        return piece_value(a.captured) * 10 - piece_value(a.moved) >
               piece_value(b.captured) * 10 - piece_value(b.moved);
    });

    for (auto &m : captures) {
        // SEE pruning: skip obviously bad captures
        if (m.captured != EMPTY && m.promotion == EMPTY) {
            int see_val = piece_value(m.captured) - piece_value(m.moved);
            if (see_val < -200) continue;
        }

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
// Alpha-Beta (inner nodes) — with TT, null move, LMR, killers, history
// ========================

int Searcher::alpha_beta(Board &board, int depth, int alpha, int beta, Move &best, int ply) {
    node_count++;
    if (time_up()) return 0;
    if (depth <= 0) return quiescence(board, alpha, beta);

    // Draw detection
    if (board.pos.halfmove_clock >= 100 || board.is_repetition() || board.is_insufficient_material()) {
        return 0;
    }

    int orig_alpha = alpha;

    // === TT Probe ===
    uint64_t hash = board.pos.hash;
    int tt_idx = (int)(hash & TT_MASK);
    TTEntry &tte = tt[tt_idx];
    Move tt_move;
    if (tte.hash == hash) {
        tt_move = tte.best;
        if (tte.depth >= depth) {
            int tt_score = score_from_tt(tte.score, ply);
            if (tte.flag == TT_EXACT) return tt_score;
            if (tte.flag == TT_BETA && tt_score >= beta) return beta;
            if (tte.flag == TT_ALPHA && tt_score <= alpha) return alpha;
        }
    }

    bool in_check_now = board.in_check(board.pos.side_to_move);

    // === Null Move Pruning ===
    if (!in_check_now && depth >= 3 && ply > 0) {
        // Check that we have non-pawn material
        Side mover = board.pos.side_to_move;
        bool has_pieces = false;
        for (int sq = 0; sq < 64; ++sq) {
            int p = board.pos.board[sq];
            if (p == EMPTY) continue;
            bool ours = (mover == WHITE) ? is_white((Piece)p) : is_black((Piece)p);
            if (!ours) continue;
            int pt = piece_type((Piece)p);
            if (pt != WP && pt != WK) { has_pieces = true; break; }
        }
        if (has_pieces) {
            int R = (depth >= 6) ? 3 : 2;
            int old_ep = board.make_null_move();
            Move dummy;
            int null_score = -alpha_beta(board, depth - 1 - R, -beta, -beta + 1, dummy, ply + 1);
            board.unmake_null_move(old_ep);
            if (stop_search) return 0;
            if (null_score >= beta) return beta;
        }
    }

    // Generate legal moves
    auto moves = generate_legal_moves(board);
    if (moves.empty()) {
        if (in_check_now) return -MATE_SCORE + ply;
        return 0;
    }

    // === Move ordering: TT move, captures (MVV-LVA), killers, history ===
    sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b) {
        return simple_move_score(board, a, tt_move, ply) > simple_move_score(board, b, tt_move, ply);
    });

    int best_score = -INF;
    int moves_searched = 0;

    for (auto &m : moves) {
        Undo u = board.make_move(m);
        bool gives_check = board.in_check(board.pos.side_to_move);

        int extension = 0;
        if (gives_check) extension = 1;
        if (in_check_now && moves.size() == 1) extension = 1; // forced reply

        Move child_best;
        int score;

        // === LMR: Late Move Reductions ===
        bool is_quiet = (m.captured == EMPTY && m.promotion == EMPTY && !gives_check);
        if (moves_searched >= 4 && depth >= 3 && is_quiet && !in_check_now) {
            int reduction = 1;
            if (moves_searched >= 10) reduction = 2;
            if (moves_searched >= 20 && depth >= 5) reduction = 3;
            // Reduced-depth zero-window search
            score = -alpha_beta(board, depth - 1 - reduction + extension, -alpha - 1, -alpha, child_best, ply + 1);
            // If it beats alpha, re-search at full depth
            if (score > alpha && !stop_search) {
                score = -alpha_beta(board, depth - 1 + extension, -beta, -alpha, child_best, ply + 1);
            }
        } else {
            score = -alpha_beta(board, depth - 1 + extension, -beta, -alpha, child_best, ply + 1);
        }

        board.unmake_move(m, u);
        moves_searched++;

        if (stop_search) return 0;
        if (score > best_score) {
            best_score = score;
            best = m;
        }
        if (score > alpha) alpha = score;
        if (alpha >= beta) {
            // Beta cutoff: update killers and history
            if (m.captured == EMPTY && ply < MAX_PLY) {
                if (!(killers[ply][0].from == m.from && killers[ply][0].to == m.to)) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                }
            }
            if (m.captured == EMPTY) {
                int side = (int)board.pos.side_to_move;
                history[side][m.from][m.to] += depth * depth;
                // Age history to prevent overflow
                if (history[side][m.from][m.to] > 400000) {
                    for (int s = 0; s < 2; ++s)
                        for (int f = 0; f < 64; ++f)
                            for (int t = 0; t < 64; ++t)
                                history[s][f][t] /= 2;
                }
            }
            break;
        }
    }

    // === TT Store ===
    tte.hash = hash;
    tte.depth = depth;
    tte.best = best;
    tte.score = score_to_tt(best_score, ply);
    if (best_score <= orig_alpha) {
        tte.flag = TT_ALPHA;
    } else if (best_score >= beta) {
        tte.flag = TT_BETA;
    } else {
        tte.flag = TT_EXACT;
    }

    return best_score;
}

// ========================
// Alpha-Beta Root — keeps complex ordering, safety checks
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
        if (in_check_now) return -MATE_SCORE;
        return 0;
    }

    // Sort with complex move_score for root
    sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b) {
        return move_score(board, a) > move_score(board, b);
    });

    // PV move from previous iteration first
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

    for (size_t i = 0; i < moves.size(); ++i) {
        auto &m = moves[i];
        Undo u = board.make_move(m);

        // Safety: check if opponent has mate in 1 (root only)
        if (has_mate_in_one(board)) {
            board.unmake_move(m, u);
            continue;
        }

        bool gives_check = board.in_check(board.pos.side_to_move);
        int extension = 0;
        if (gives_check) {
            bool checker_attacked = board.is_square_attacked(m.to, board.pos.side_to_move);
            bool checker_defended = board.is_square_attacked(m.to, opposite(board.pos.side_to_move));
            if (!checker_attacked || checker_defended) extension = 1;
        }

        // Blunder filter via SEE
        if (!gives_check) {
            int pt_moved = piece_type((Piece)m.moved);
            int see = see_score(board, m);
            if ((pt_moved == WQ || pt_moved == WR || pt_moved == WB || pt_moved == WN) && see < -200) {
                board.unmake_move(m, u);
                continue;
            }
        }

        Move child_best;
        int search_alpha = collect_all ? max(-INF, best_score - 50) : alpha;
        int score = -alpha_beta(board, depth - 1 + extension, -beta, -search_alpha, child_best, 1);

        board.unmake_move(m, u);

        // Post-search penalty: queen deep pawn grab
        {
            int pt = piece_type((Piece)m.moved);
            if (pt == WQ && m.captured != EMPTY && piece_type((Piece)m.captured) == WP) {
                int to_rank = rank_of(m.to);
                bool queen_w = is_white((Piece)m.moved);
                bool deep = queen_w ? (to_rank >= 6) : (to_rank <= 1);
                if (deep) {
                    int undev = 0;
                    if (queen_w) {
                        if (board.pos.board[make_sq(1,0)] == WN) undev++;
                        if (board.pos.board[make_sq(6,0)] == WN) undev++;
                        if (board.pos.board[make_sq(2,0)] == WB) undev++;
                        if (board.pos.board[make_sq(5,0)] == WB) undev++;
                    } else {
                        if (board.pos.board[make_sq(1,7)] == BN) undev++;
                        if (board.pos.board[make_sq(6,7)] == BN) undev++;
                        if (board.pos.board[make_sq(2,7)] == BB) undev++;
                        if (board.pos.board[make_sq(5,7)] == BB) undev++;
                    }
                    score -= 150 + undev * 100;
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

    // Clear killers and age history for this search
    memset(killers, 0, sizeof(killers));
    for (int s = 0; s < 2; ++s)
        for (int f = 0; f < 64; ++f)
            for (int t = 0; t < 64; ++t)
                history[s][f][t] /= 4;

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
        if (best_mate_score >= MATE_SCORE - 20) return best_mate_move;
    }

    // === QUICK CHECK: Obvious promotion ===
    {
        auto moves = generate_legal_moves(board);
        for (auto &m : moves) {
            if (m.promotion == WQ || m.promotion == BQ) {
                Undo u = board.make_move(m);
                bool queen_safe = !board.is_square_attacked(m.to, board.pos.side_to_move);
                board.unmake_move(m, u);
                if (queen_safe) return m;
            }
        }
    }

    // === Iterative Deepening with Aspiration Windows ===
    node_count = 0;
    Move prev_best;
    vector<pair<Move, int>> root_scores;
    vector<pair<Move, int>> last_complete_root_scores;
    int prev_score = 0;

    for (int depth = 1; depth <= max_depth; ++depth) {
        Move curr_best;
        root_scores.clear();

        int score;
        if (depth >= 4 && !stop_search) {
            // Aspiration window
            int window = 50;
            int a = prev_score - window;
            int b = prev_score + window;
            score = alpha_beta_root(board, depth, a, b, curr_best, root_scores, prev_best);
            // If outside window, re-search with full window
            if (!stop_search && (score <= a || score >= b)) {
                root_scores.clear();
                score = alpha_beta_root(board, depth, -INF, INF, curr_best, root_scores, prev_best);
            }
        } else {
            score = alpha_beta_root(board, depth, -INF, INF, curr_best, root_scores, prev_best);
        }

        if (stop_search) {
            if (prev_best.from != -1) best = prev_best;
            else if (curr_best.from != -1) best = curr_best;
            break;
        }
        best = curr_best;
        prev_best = curr_best;
        best_score = score;
        prev_score = score;
        depth_reached = depth;
        last_complete_root_scores = root_scores;

        auto now = chrono::steady_clock::now();
        int elapsed = (int)chrono::duration_cast<chrono::milliseconds>(now - search_start).count();

        cout << "info depth " << depth << " score cp " << score
             << " nodes " << node_count
             << " time " << elapsed
             << " pv " << move_to_uci(curr_best) << endl;

        // If we've found a forced mate, stop searching
        if (score >= MATE_SCORE - 100 || score <= -MATE_SCORE + 100) break;

        // Don't start a new iteration if we've used > 50% of time
        if (elapsed > time_limit_ms / 2) break;
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
    auto time_remaining = [&]() -> int {
        auto now = chrono::steady_clock::now();
        return time_limit - (int)chrono::duration_cast<chrono::milliseconds>(now - search_start).count();
    };

    if (best.from != -1 && !stop_search && time_remaining() > time_limit * 2 / 5) {
        Undo u = board.make_move(best);
        bool allows_mate1 = opponent_has_mate_in_one(board);
        int opp_mate = 0;
        if (!allows_mate1 && time_remaining() > time_limit / 3) {
            opp_mate = opponent_has_mate_in_n(board, 2);
        }
        if (allows_mate1 || opp_mate >= MATE_SCORE - 20) {
            board.unmake_move(best, u);
            cerr << "info string WARNING: Best move allows mate, finding alternative" << endl;
            auto moves = generate_legal_moves(board);
            Move safe_best;
            int safe_score = -INF;
            for (auto &m : moves) {
                if (time_remaining() <= 20) break;
                Undo u2 = board.make_move(m);
                bool m_allows_mate1 = opponent_has_mate_in_one(board);
                int m_opp_mate = m_allows_mate1 ? MATE_SCORE : opponent_has_mate_in_n(board, 2);
                int score;
                if (m_allows_mate1 || m_opp_mate >= MATE_SCORE - 20) {
                    score = -MATE_SCORE;
                } else if (time_remaining() > 20) {
                    Move child_best;
                    score = -alpha_beta(board, 3, -INF, INF, child_best, 0);
                } else {
                    score = 0;
                }
                board.unmake_move(m, u2);
                if (score > safe_score) { safe_score = score; safe_best = m; }
            }
            if (safe_best.from != -1) best = safe_best;
        } else {
            board.unmake_move(best, u);
        }
    }

    if (best.from != -1 && !stop_search && time_remaining() > time_limit * 2 / 5) {
        if (is_mate_trap(board, best, MATE_TRAP_PLIES)) {
            auto moves = generate_legal_moves(board);
            Move alt_best = best;
            int alt_score = -INF;
            int alt_depth = max(1, min(3, depth_reached - 1));
            for (auto &m : moves) {
                if (time_up() || time_remaining() <= 20) break;
                if (is_mate_trap(board, m, MATE_TRAP_PLIES)) continue;
                Undo u = board.make_move(m);
                Move child_best;
                int score = -alpha_beta(board, alt_depth, -INF, INF, child_best, 0);
                board.unmake_move(m, u);
                if (score > alt_score) { alt_score = score; alt_best = m; }
            }
            if (alt_score > -INF / 2) best = alt_best;
        }
    }

    if (best.from == -1) {
        auto moves = generate_legal_moves(board);
        if (!moves.empty()) best = moves[0];
    }
    return best;
}

} // namespace chess
