#pragma once

#include "types.h"

namespace chess {

class Board;

void init_pst_tables();
int evaluate(Board &board);

} // namespace chess
