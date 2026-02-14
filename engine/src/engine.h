#pragma once

#include "position.h"
#include "search.h"
#include "book.h"
#include <string>

namespace chess {

class Engine {
public:
    Engine();

    void set_startpos();
    void new_game();
    void set_position_from_uci(const std::string &line);
    void apply_uci_move(const std::string &mv);
    std::vector<std::string> legal_moves_uci();

    Move search_bestmove(int max_depth, int time_limit_ms);
    std::string probe_book() const;
    int compute_time_ms(int wtime, int btime, int winc, int binc) const;
    std::string move_to_uci_public(const Move &m) const;
    bool has_legal_moves();
    bool check_insufficient_material() const;

private:
public:
    Board board;
    Searcher searcher;
    OpeningBook book;
};

} // namespace chess
