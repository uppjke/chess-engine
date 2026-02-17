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
}

void Searcher::new_game() {
    fill(tt.begin(), tt.end(), TTEntry{});
    memset(history, 0, sizeof(history));
    memset(killers, 0, sizeof(killers));
    memset(countermoves, 0, sizeof(countermoves));
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

    // Countermove heuristic: the expected response to opponent's last move
    const Move &prev = (board.pos.side_to_move == WHITE) ? board.last_move_black : board.last_move_white;
    if (prev.from >= 0 && prev.from < 64 && prev.to >= 0 && prev.to < 64) {
        const Move &cm = countermoves[prev.from][prev.to];
        if (cm.from == m.from && cm.to == m.to) return 3800000;
    }

    // History heuristic
    int side = (int)board.pos.side_to_move;
    return history[side][m.from][m.to];
}

// ========================
// Complex Move Score (root only — move ordering)
// ========================

int Searcher::move_score(const Board &board, const Move &m) const {
    int score = 0;
    const auto &pos = board.pos;
    Side mover = (m.moved <= WK) ? WHITE : BLACK;
    Side opp = (mover == WHITE) ? BLACK : WHITE;
    int pt = piece_type((Piece)m.moved);

    // === Promotions ===
    if (m.promotion != EMPTY) {
        score += 8000 + piece_value(m.promotion);
    }

    // === Captures — MVV-LVA ===
    if (m.captured != EMPTY) {
        int their_value = piece_value(m.captured);
        int our_value = piece_value(m.moved);
        bool is_defended = board.is_square_attacked(m.to, opp);
        if (!is_defended) {
            score += 5000 + their_value * 10 - our_value;
        } else if (our_value <= their_value + 50) {
            score += 4000 + their_value - our_value;
        } else {
            score += 2000 + their_value - our_value;
        }
    }

    // === Castling ===
    if (m.is_castle) score += 500;

    // === Avoid moving to attacked undefended squares ===
    if (m.captured == EMPTY && board.is_square_attacked(m.to, opp)) {
        if (!board.is_square_attacked(m.to, mover)) {
            if (pt == WQ) score -= 2500;
            else if (pt == WR) score -= 800;
            else if (pt == WB || pt == WN) score -= 400;
        }
    }

    // === Penalize king moves if castling available ===
    if (pt == WK && !m.is_castle) {
        bool can_k = (mover == WHITE) ? (pos.castling_rights & 1) : (pos.castling_rights & 4);
        bool can_q = (mover == WHITE) ? (pos.castling_rights & 2) : (pos.castling_rights & 8);
        if (can_k || can_q) score -= 300;
    }

    // === Penalize reverse/repeated moves ===
    if (is_reverse_of_last(board, m)) score -= reverse_move_penalty(m);
    if (is_repeat_piece_move(board, m)) score -= 200;

    // === Development bonuses in opening ===
    if (pos.fullmove_number <= 12 && m.captured == EMPTY) {
        int to_rank = rank_of(m.to);
        int to_file = file_of(m.to);
        if (pt == WN) {
            if (to_file >= 2 && to_file <= 5 && to_rank >= 2 && to_rank <= 5) score += 60;
        }
        if (pt == WB) {
            if (to_file >= 1 && to_file <= 6 && to_rank >= 1 && to_rank <= 6) score += 50;
        }
        if (pt == WP && (to_file == 3 || to_file == 4)) {
            if ((mover == WHITE && (to_rank == 2 || to_rank == 3)) ||
                (mover == BLACK && (to_rank == 4 || to_rank == 5))) score += 60;
        }
        // Penalize rook moves in opening
        if (pt == WR && pos.fullmove_number <= 10) score -= 300;
        // Penalize queen moves in early opening
        if (pt == WQ && pos.fullmove_number <= 6) score -= 200;
        // Penalize flank pawn moves
        if (pt == WP) {
            int from_file = file_of(m.from);
            if (from_file == 0 || from_file == 7) score -= 150;
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

    // Delta pruning — if even capturing a queen can't raise alpha
    if (stand + 1025 < alpha) return stand;

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

        // Delta pruning per-move
        if (m.promotion == EMPTY && stand + piece_value(m.captured) + 200 < alpha) continue;

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

    // === IID: Internal Iterative Deepening ===
    // When no TT move and depth is high, do a shallow search to get a move for ordering
    if (tt_move.from == -1 && depth >= 5 && !in_check_now) {
        Move iid_best;
        alpha_beta(board, depth - 4, alpha, beta, iid_best, ply);
        if (stop_search) return 0;
        // Re-probe TT — the shallow search should have stored something
        if (tt[tt_idx].hash == hash) {
            tt_move = tt[tt_idx].best;
        }
    }

    // === Static eval (compute once, reuse for all pruning) ===
    int static_eval = 0;
    bool eval_computed = false;
    if (!in_check_now && ply > 0) {
        static_eval = evaluate(board);
        eval_computed = true;
    }

    // === Reverse Futility Pruning (Static Null Move Pruning) ===
    // Skip when static eval is not reliable (e.g. king safety is very poor)
    if (!in_check_now && ply > 0 && depth <= 6 && eval_computed) {
        int rfp_margin = depth * 80;
        // Don't prune if eval is close to mate territory
        if (static_eval - rfp_margin >= beta && static_eval < MATE_SCORE - 500 && static_eval > -MATE_SCORE + 500) {
            return static_eval;
        }
    }

    // === Null Move Pruning ===
    if (!in_check_now && depth >= 3 && ply > 0 && eval_computed && static_eval >= beta) {
        // Quick check for non-pawn material (avoid zugzwang)
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
            int R = 3 + depth / 4;  // More aggressive: R=3+depth/4
            if (R > depth - 1) R = depth - 1;
            int old_ep = board.make_null_move();
            Move dummy;
            int null_score = -alpha_beta(board, depth - 1 - R, -beta, -beta + 1, dummy, ply + 1);
            board.unmake_null_move(old_ep);
            if (stop_search) return 0;
            if (null_score >= beta) return beta;
        }
    }

    // === Futility / Razoring ===
    bool do_futility = false;
    if (!in_check_now && ply > 0 && depth <= 3 && eval_computed) {
        // Don't apply futility pruning if eval suggests possible mate threats
        if (alpha > -MATE_SCORE + 500 && beta < MATE_SCORE - 500) {
            static const int futility_margin[] = {0, 200, 350, 500};
            if (static_eval + futility_margin[depth] <= alpha) {
                do_futility = true;
            }
        }
    }

    if (!in_check_now && ply > 0 && depth <= 2 && !do_futility && eval_computed) {
        int razor_margin = (depth == 1) ? 300 : 600;
        if (static_eval + razor_margin <= alpha) {
            int q_score = quiescence(board, alpha, beta);
            if (q_score <= alpha) return q_score;
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
        bool is_quiet = (m.captured == EMPTY && m.promotion == EMPTY);

        // Futility pruning: skip quiet moves that can't raise alpha
        if (do_futility && is_quiet && moves_searched > 0 && !in_check_now) {
            moves_searched++;
            continue;
        }

        // Late Move Pruning (LMP): at low depth, skip very late quiet moves
        if (is_quiet && !in_check_now && depth <= 4 && moves_searched > 0) {
            static const int lmp_count[] = {0, 5, 8, 13, 20};
            if (moves_searched >= lmp_count[depth]) {
                moves_searched++;
                continue;
            }
        }

        Undo u = board.make_move(m);
        bool gives_check = board.in_check(board.pos.side_to_move);

        // Skip futility-pruned moves that give check (we want to search those)
        // Re-enable search for checking moves
        int extension = 0;
        if (gives_check) extension = 1;
        if (in_check_now && moves.size() == 1) extension = 1;

        Move child_best;
        int score;

        // === PVS + LMR ===
        if (moves_searched == 0) {
            // First move: full window search
            score = -alpha_beta(board, depth - 1 + extension, -beta, -alpha, child_best, ply + 1);
        } else {
            // LMR for late quiet moves
            bool use_lmr = (moves_searched >= 3 && depth >= 3 && is_quiet && !in_check_now && !gives_check);
            int reduction = 0;
            if (use_lmr) {
                // Log-based LMR: reduction = sqrt(depth) * sqrt(moveNumber) / 2
                reduction = 1;
                if (moves_searched >= 6) reduction = 2;
                if (moves_searched >= 12 && depth >= 5) reduction = 3;
                if (moves_searched >= 20 && depth >= 7) reduction = 4;
                // Don't reduce below 1
                if (depth - 1 - reduction + extension < 1) reduction = depth - 2 + extension;
                if (reduction < 0) reduction = 0;
            }

            // Zero-window search (PVS) with possible LMR
            score = -alpha_beta(board, depth - 1 - reduction + extension, -alpha - 1, -alpha, child_best, ply + 1);

            // If ZWS fails high, re-search with full window (and full depth if LMR was used)
            if (score > alpha && !stop_search) {
                score = -alpha_beta(board, depth - 1 + extension, -beta, -alpha, child_best, ply + 1);
            }
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
            // Beta cutoff: update killers, history, and countermove
            if (m.captured == EMPTY && ply < MAX_PLY) {
                if (!(killers[ply][0].from == m.from && killers[ply][0].to == m.to)) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                }
            }
            if (m.captured == EMPTY) {
                int side = (int)board.pos.side_to_move;
                history[side][m.from][m.to] += depth * depth;
                if (history[side][m.from][m.to] > 400000) {
                    for (int s = 0; s < 2; ++s)
                        for (int f = 0; f < 64; ++f)
                            for (int t = 0; t < 64; ++t)
                                history[s][f][t] /= 2;
                }
                // Countermove: store this move as the best response to opponent's last move
                const Move &prev = (board.pos.side_to_move == WHITE) ? board.last_move_black : board.last_move_white;
                if (prev.from >= 0 && prev.from < 64 && prev.to >= 0 && prev.to < 64) {
                    countermoves[prev.from][prev.to] = m;
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

    for (size_t i = 0; i < moves.size(); ++i) {
        auto &m = moves[i];
        Undo u = board.make_move(m);

        bool gives_check = board.in_check(board.pos.side_to_move);
        int extension = 0;
        if (gives_check) extension = 1;

        Move child_best;
        int score;

        // PVS at root: first move full window, rest zero-window + re-search
        if (i == 0) {
            score = -alpha_beta(board, depth - 1 + extension, -beta, -alpha, child_best, 1);
        } else {
            score = -alpha_beta(board, depth - 1 + extension, -alpha - 1, -alpha, child_best, 1);
            if (score > alpha && score < beta && !stop_search) {
                score = -alpha_beta(board, depth - 1 + extension, -beta, -alpha, child_best, 1);
            }
        }

        board.unmake_move(m, u);

        root_scores.push_back({m, score});
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

    // === Iterative Deepening with Aspiration Windows ===
    node_count = 0;
    Move prev_best;
    vector<pair<Move, int>> root_scores;

    int prev_score = 0;

    for (int depth = 1; depth <= max_depth; ++depth) {
        Move curr_best;
        root_scores.clear();

        int score;
        if (depth >= 4 && !stop_search) {
            // Aspiration window — widening
            int window = 35;
            int a = prev_score - window;
            int b = prev_score + window;
            score = alpha_beta_root(board, depth, a, b, curr_best, root_scores, prev_best);
            // If outside window, widen and retry
            if (!stop_search && (score <= a || score >= b)) {
                window *= 4;
                a = prev_score - window;
                b = prev_score + window;
                root_scores.clear();
                score = alpha_beta_root(board, depth, a, b, curr_best, root_scores, prev_best);
            }
            // Full window fallback
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

        auto now = chrono::steady_clock::now();
        int elapsed = (int)chrono::duration_cast<chrono::milliseconds>(now - search_start).count();

        cout << "info depth " << depth << " score cp " << score
             << " nodes " << node_count
             << " time " << elapsed
             << " pv " << move_to_uci(curr_best) << endl;

        // If we've found a forced mate, stop searching
        if (score >= MATE_SCORE - 100 || score <= -MATE_SCORE + 100) break;

        // Don't start a new iteration if we've used > 40% of time (save for deeper search)
        if (elapsed > time_limit_ms * 2 / 5) break;
    }

    if (best.from == -1) {
        auto moves = generate_legal_moves(board);
        if (!moves.empty()) best = moves[0];
    }
    return best;
}

} // namespace chess
