#pragma once

#include "types.h"
#include <chrono>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chess {

class Board;

class Searcher {
public:
    Searcher();

    Move search_bestmove(Board &board, int max_depth, int time_limit_ms);
    void new_game();

    std::unordered_map<uint64_t, TTEntry> tt;
    bool opening_variety = true;

private:
    // Move ordering
    int move_score(const Board &board, const Move &m) const;
    bool is_reverse_of_last(const Board &board, const Move &m) const;
    int reverse_move_penalty(const Move &m) const;
    bool is_repeat_piece_move(const Board &board, const Move &m) const;
    int see_score(const Board &board, const Move &m) const;

    // Search
    int alpha_beta_root(Board &board, int depth, int alpha, int beta, Move &best,
                        std::vector<std::pair<Move, int>> &root_scores, const Move &prev_best);
    int alpha_beta(Board &board, int depth, int alpha, int beta, Move &best);
    int quiescence(Board &board, int alpha, int beta);

    // Mate detection
    bool has_mate_in_one(Board &board);
    int mate_search(Board &board, int depth, bool maximizing);
    int has_mate_in_n(Board &board, int depth);
    int opponent_has_mate_in_n(Board &board, int depth);
    bool opponent_has_mate_in_one(Board &board);
    bool can_force_mate(Board &board, int plies);
    bool is_mate_trap(Board &board, const Move &m, int plies);

    // Time
    bool time_up();

    // State
    std::chrono::steady_clock::time_point search_start;
    int time_limit = 1000;
    bool stop_search = false;
    int node_count = 0;
    int mate_probe_nodes = 0;
    int mate_search_nodes = 0;
    std::mt19937 opening_rng;

    static constexpr int MATE_TRAP_PLIES = 6;
    static constexpr int MATE_PROBE_LIMIT = 8000;
    static constexpr int MATE_SEARCH_NODE_LIMIT = 5000;
};

} // namespace chess
