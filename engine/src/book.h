#pragma once

#include "types.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace chess {

class Board;

struct BookEntry {
    std::string move;  // UCI format e.g. "e2e4"
    int weight;        // relative probability
};

class OpeningBook {
public:
    OpeningBook();

    // Returns a book move in UCI format, or "" if not in book
    std::string probe(uint64_t hash) const;

    bool in_book(uint64_t hash) const;

private:
    void add_line(const std::string &moves, int weight = 100);
    void init_book();

    // hash -> list of candidate moves with weights
    std::unordered_map<uint64_t, std::vector<BookEntry>> book_;
};

} // namespace chess
