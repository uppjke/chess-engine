#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace chess {

class Board {
public:
    Board();

    void parse_fen(const std::string &fen);
    std::string to_fen() const;

    Undo make_move(const Move &m);
    void unmake_move(const Move &m, const Undo &u);

    bool is_square_attacked(int sq, Side by) const;
    bool in_check(Side side) const;

    void update_hash();
    bool is_repetition() const;
    bool is_insufficient_material() const;

    // Null move (for null move pruning)
    int make_null_move();
    void unmake_null_move(int old_ep);

    // Public state
    Position pos;
    HashKeys keys;
    std::vector<uint64_t> hash_history;
    Move last_move_white{};
    Move last_move_black{};
};

} // namespace chess
