#include "movegen.h"
#include "position.h"

using namespace std;

namespace chess {

// File-local helpers
static void add_promo_moves(const Board &board, vector<Move> &moves, int from, int to, int captured = EMPTY) {
    int promos[4] = {WQ, WR, WB, WN};
    if (board.pos.side_to_move == BLACK) {
        promos[0] = BQ; promos[1] = BR; promos[2] = BB; promos[3] = BN;
    }
    for (int pr : promos) {
        Move m;
        m.from = from;
        m.to = to;
        m.promotion = pr;
        m.captured = captured;
        m.moved = board.pos.board[from];
        moves.push_back(m);
    }
}

static void add_castling_moves(const Board &board, vector<Move> &moves, int king_sq, int king_piece) {
    if (king_piece == WK) {
        if ((board.pos.castling_rights & 1) && board.pos.board[make_sq(5,0)] == EMPTY && board.pos.board[make_sq(6,0)] == EMPTY) {
            if (!board.is_square_attacked(make_sq(4,0), BLACK) && !board.is_square_attacked(make_sq(5,0), BLACK) && !board.is_square_attacked(make_sq(6,0), BLACK)) {
                Move m{king_sq, make_sq(6,0), EMPTY, false, true, EMPTY, king_piece};
                moves.push_back(m);
            }
        }
        if ((board.pos.castling_rights & 2) && board.pos.board[make_sq(3,0)] == EMPTY && board.pos.board[make_sq(2,0)] == EMPTY && board.pos.board[make_sq(1,0)] == EMPTY) {
            if (!board.is_square_attacked(make_sq(4,0), BLACK) && !board.is_square_attacked(make_sq(3,0), BLACK) && !board.is_square_attacked(make_sq(2,0), BLACK)) {
                Move m{king_sq, make_sq(2,0), EMPTY, false, true, EMPTY, king_piece};
                moves.push_back(m);
            }
        }
    } else if (king_piece == BK) {
        if ((board.pos.castling_rights & 4) && board.pos.board[make_sq(5,7)] == EMPTY && board.pos.board[make_sq(6,7)] == EMPTY) {
            if (!board.is_square_attacked(make_sq(4,7), WHITE) && !board.is_square_attacked(make_sq(5,7), WHITE) && !board.is_square_attacked(make_sq(6,7), WHITE)) {
                Move m{king_sq, make_sq(6,7), EMPTY, false, true, EMPTY, king_piece};
                moves.push_back(m);
            }
        }
        if ((board.pos.castling_rights & 8) && board.pos.board[make_sq(3,7)] == EMPTY && board.pos.board[make_sq(2,7)] == EMPTY && board.pos.board[make_sq(1,7)] == EMPTY) {
            if (!board.is_square_attacked(make_sq(4,7), WHITE) && !board.is_square_attacked(make_sq(3,7), WHITE) && !board.is_square_attacked(make_sq(2,7), WHITE)) {
                Move m{king_sq, make_sq(2,7), EMPTY, false, true, EMPTY, king_piece};
                moves.push_back(m);
            }
        }
    }
}

void generate_pseudo_moves(const Board &board, vector<Move> &moves) {
    moves.clear();
    const auto &pos = board.pos;
    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p == EMPTY) continue;
        if (pos.side_to_move == WHITE && !is_white((Piece)p)) continue;
        if (pos.side_to_move == BLACK && !is_black((Piece)p)) continue;

        int f = file_of(sq);
        int r = rank_of(sq);

        if (p == WP || p == BP) {
            int dir = (p == WP) ? 1 : -1;
            int start_rank = (p == WP) ? 1 : 6;
            int promo_rank = (p == WP) ? 6 : 1;
            int next_rank = r + dir;
            if (next_rank >= 0 && next_rank <= 7) {
                int forward = make_sq(f, next_rank);
                if (pos.board[forward] == EMPTY) {
                    if (r == promo_rank) {
                        add_promo_moves(board, moves, sq, forward);
                    } else {
                        moves.push_back({sq, forward, EMPTY, false, false, EMPTY, p});
                        if (r == start_rank) {
                            int two_rank = r + 2 * dir;
                            int forward2 = make_sq(f, two_rank);
                            if (pos.board[forward2] == EMPTY) {
                                moves.push_back({sq, forward2, EMPTY, false, false, EMPTY, p});
                            }
                        }
                    }
                }
            }
            int cap_rank = r + dir;
            if (cap_rank >= 0 && cap_rank <= 7) {
                for (int df : {-1, 1}) {
                    int nf = f + df;
                    if (nf < 0 || nf > 7) continue;
                    int to = make_sq(nf, cap_rank);
                    int target = pos.board[to];
                    if (target != EMPTY && ((p == WP && is_black((Piece)target)) || (p == BP && is_white((Piece)target)))) {
                        if (r == promo_rank) {
                            add_promo_moves(board, moves, sq, to, target);
                        } else {
                            moves.push_back({sq, to, EMPTY, false, false, target, p});
                        }
                    }
                }
            }
            if (pos.ep_square != -1) {
                int ep_rank = r + dir;
                if (ep_rank >= 0 && ep_rank <= 7) {
                    for (int df : {-1, 1}) {
                        int nf = f + df;
                        if (nf < 0 || nf > 7) continue;
                        int to = make_sq(nf, ep_rank);
                        if (to == pos.ep_square) {
                            Move m;
                            m.from = sq;
                            m.to = to;
                            m.is_en_passant = true;
                            m.moved = p;
                            m.captured = (p == WP) ? BP : WP;
                            moves.push_back(m);
                        }
                    }
                }
            }
        } else if (p == WN || p == BN) {
            static const int kofs[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
            for (auto &o : kofs) {
                int nf = f + o[0];
                int nr = r + o[1];
                if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
                int to = make_sq(nf, nr);
                int target = pos.board[to];
                if (target == EMPTY || is_opponent(p, target)) {
                    moves.push_back({sq, to, EMPTY, false, false, target, p});
                }
            }
        } else if (p == WB || p == BB || p == WR || p == BR || p == WQ || p == BQ) {
            vector<pair<int,int>> dirs;
            if (p == WB || p == BB || p == WQ || p == BQ) {
                dirs.push_back({1,1}); dirs.push_back({1,-1}); dirs.push_back({-1,1}); dirs.push_back({-1,-1});
            }
            if (p == WR || p == BR || p == WQ || p == BQ) {
                dirs.push_back({1,0}); dirs.push_back({-1,0}); dirs.push_back({0,1}); dirs.push_back({0,-1});
            }
            for (auto &d : dirs) {
                int nf = f + d.first;
                int nr = r + d.second;
                while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
                    int to = make_sq(nf, nr);
                    int target = pos.board[to];
                    if (target == EMPTY) {
                        moves.push_back({sq, to, EMPTY, false, false, EMPTY, p});
                    } else {
                        if (is_opponent(p, target)) {
                            moves.push_back({sq, to, EMPTY, false, false, target, p});
                        }
                        break;
                    }
                    nf += d.first;
                    nr += d.second;
                }
            }
        } else if (p == WK || p == BK) {
            static const int dirs[8][2] = {{1,1},{1,0},{1,-1},{0,1},{0,-1},{-1,1},{-1,0},{-1,-1}};
            for (auto &d : dirs) {
                int nf = f + d[0];
                int nr = r + d[1];
                if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
                int to = make_sq(nf, nr);
                int target = pos.board[to];
                if (target == EMPTY || is_opponent(p, target)) {
                    moves.push_back({sq, to, EMPTY, false, false, target, p});
                }
            }
            add_castling_moves(board, moves, sq, p);
        }
    }
}

vector<Move> generate_legal_moves(Board &board) {
    vector<Move> moves;
    generate_pseudo_moves(board, moves);
    vector<Move> legal;
    for (auto &m : moves) {
        Undo u = board.make_move(m);
        if (!board.in_check(opposite(board.pos.side_to_move))) {
            legal.push_back(m);
        }
        board.unmake_move(m, u);
    }
    return legal;
}

} // namespace chess
