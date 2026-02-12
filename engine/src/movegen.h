#pragma once

#include "types.h"
#include <vector>

namespace chess {

class Board;

void generate_pseudo_moves(const Board &board, std::vector<Move> &moves);
std::vector<Move> generate_legal_moves(Board &board);

} // namespace chess
