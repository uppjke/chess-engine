#include "position.h"
#include <sstream>

using namespace std;

namespace chess {

Board::Board() {
    pos.board.fill(EMPTY);
    last_move_white.from = -1;
    last_move_black.from = -1;
}

void Board::parse_fen(const string &fen) {
    pos.board.fill(EMPTY);
    pos.side_to_move = WHITE;
    pos.castling_rights = 0;
    pos.ep_square = -1;
    pos.halfmove_clock = 0;
    pos.fullmove_number = 1;

    stringstream ss(fen);
    string board_part, side_part, castling_part, ep_part;
    ss >> board_part >> side_part >> castling_part >> ep_part >> pos.halfmove_clock >> pos.fullmove_number;

    int sq = 56;
    for (char c : board_part) {
        if (c == '/') {
            sq -= 16;
            continue;
        }
        if (c >= '1' && c <= '8') {
            sq += (c - '0');
            continue;
        }
        pos.board[sq] = char_to_piece(c);
        sq++;
    }

    pos.side_to_move = (side_part == "w") ? WHITE : BLACK;
    if (castling_part.find('K') != string::npos) pos.castling_rights |= 1;
    if (castling_part.find('Q') != string::npos) pos.castling_rights |= 2;
    if (castling_part.find('k') != string::npos) pos.castling_rights |= 4;
    if (castling_part.find('q') != string::npos) pos.castling_rights |= 8;

    if (ep_part != "-") {
        pos.ep_square = str_to_sq(ep_part);
    }
    update_hash();
    hash_history.clear();
    hash_history.push_back(pos.hash);
    last_move_white = Move{};
    last_move_black = Move{};
    last_move_white.from = -1;
    last_move_black.from = -1;
}

string Board::to_fen() const {
    string out;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            int sq = make_sq(f, r);
            int p = pos.board[sq];
            if (p == EMPTY) {
                empty++;
            } else {
                if (empty > 0) { out += char('0' + empty); empty = 0; }
                out += piece_to_char((Piece)p);
            }
        }
        if (empty > 0) out += char('0' + empty);
        if (r > 0) out += '/';
    }
    out += pos.side_to_move == WHITE ? " w " : " b ";
    string castle;
    if (pos.castling_rights & 1) castle += 'K';
    if (pos.castling_rights & 2) castle += 'Q';
    if (pos.castling_rights & 4) castle += 'k';
    if (pos.castling_rights & 8) castle += 'q';
    out += castle.empty() ? "-" : castle;
    out += ' ';
    out += pos.ep_square == -1 ? "-" : sq_to_str(pos.ep_square);
    out += ' ';
    out += to_string(pos.halfmove_clock);
    out += ' ';
    out += to_string(pos.fullmove_number);
    return out;
}

Undo Board::make_move(const Move &m) {
    Undo u{pos.castling_rights, pos.ep_square, pos.halfmove_clock, pos.fullmove_number, m.captured, last_move_white, last_move_black};
    Side mover_side = pos.side_to_move;
    int moved_piece = pos.board[m.from];
    pos.board[m.from] = EMPTY;

    if (m.is_en_passant) {
        int cap_sq = m.to + (pos.side_to_move == WHITE ? -8 : 8);
        pos.board[cap_sq] = EMPTY;
    }

    if (m.is_castle) {
        if (moved_piece == WK && m.to == make_sq(6,0)) {
            pos.board[make_sq(5,0)] = WR;
            pos.board[make_sq(7,0)] = EMPTY;
        } else if (moved_piece == WK && m.to == make_sq(2,0)) {
            pos.board[make_sq(3,0)] = WR;
            pos.board[make_sq(0,0)] = EMPTY;
        } else if (moved_piece == BK && m.to == make_sq(6,7)) {
            pos.board[make_sq(5,7)] = BR;
            pos.board[make_sq(7,7)] = EMPTY;
        } else if (moved_piece == BK && m.to == make_sq(2,7)) {
            pos.board[make_sq(3,7)] = BR;
            pos.board[make_sq(0,7)] = EMPTY;
        }
    }

    int placed_piece = moved_piece;
    if (m.promotion != EMPTY) {
        placed_piece = m.promotion;
    }
    pos.board[m.to] = placed_piece;

    pos.ep_square = -1;
    if (moved_piece == WP && rank_of(m.from) == 1 && rank_of(m.to) == 3) {
        pos.ep_square = m.from + 8;
    } else if (moved_piece == BP && rank_of(m.from) == 6 && rank_of(m.to) == 4) {
        pos.ep_square = m.from - 8;
    }

    if (moved_piece == WK) pos.castling_rights &= ~3;
    if (moved_piece == BK) pos.castling_rights &= ~12;
    if (moved_piece == WR && m.from == make_sq(0,0)) pos.castling_rights &= ~2;
    if (moved_piece == WR && m.from == make_sq(7,0)) pos.castling_rights &= ~1;
    if (moved_piece == BR && m.from == make_sq(0,7)) pos.castling_rights &= ~8;
    if (moved_piece == BR && m.from == make_sq(7,7)) pos.castling_rights &= ~4;

    if (m.captured == WR && m.to == make_sq(0,0)) pos.castling_rights &= ~2;
    if (m.captured == WR && m.to == make_sq(7,0)) pos.castling_rights &= ~1;
    if (m.captured == BR && m.to == make_sq(0,7)) pos.castling_rights &= ~8;
    if (m.captured == BR && m.to == make_sq(7,7)) pos.castling_rights &= ~4;

    if (moved_piece == WP || moved_piece == BP || m.captured != EMPTY) {
        pos.halfmove_clock = 0;
    } else {
        pos.halfmove_clock++;
    }

    if (pos.side_to_move == BLACK) pos.fullmove_number++;
    pos.side_to_move = opposite(pos.side_to_move);
    update_hash();
    hash_history.push_back(pos.hash);
    if (mover_side == WHITE) last_move_white = m;
    else last_move_black = m;
    return u;
}

void Board::unmake_move(const Move &m, const Undo &u) {
    pos.side_to_move = opposite(pos.side_to_move);
    if (pos.side_to_move == BLACK) pos.fullmove_number--;

    pos.castling_rights = u.castling;
    pos.ep_square = u.ep_square;
    pos.halfmove_clock = u.halfmove;
    pos.fullmove_number = u.fullmove;

    int moved_piece = pos.board[m.to];
    pos.board[m.from] = (m.promotion != EMPTY) ? (pos.side_to_move == WHITE ? WP : BP) : moved_piece;

    if (m.is_castle) {
        if (moved_piece == WK && m.to == make_sq(6,0)) {
            pos.board[make_sq(7,0)] = WR;
            pos.board[make_sq(5,0)] = EMPTY;
        } else if (moved_piece == WK && m.to == make_sq(2,0)) {
            pos.board[make_sq(0,0)] = WR;
            pos.board[make_sq(3,0)] = EMPTY;
        } else if (moved_piece == BK && m.to == make_sq(6,7)) {
            pos.board[make_sq(7,7)] = BR;
            pos.board[make_sq(5,7)] = EMPTY;
        } else if (moved_piece == BK && m.to == make_sq(2,7)) {
            pos.board[make_sq(0,7)] = BR;
            pos.board[make_sq(3,7)] = EMPTY;
        }
    }

    if (m.is_en_passant) {
        int cap_sq = m.to + (pos.side_to_move == WHITE ? -8 : 8);
        pos.board[cap_sq] = (pos.side_to_move == WHITE ? BP : WP);
        pos.board[m.to] = EMPTY;
    } else {
        pos.board[m.to] = m.captured;
    }
    last_move_white = u.last_move_white;
    last_move_black = u.last_move_black;
    update_hash();
    if (!hash_history.empty()) {
        hash_history.pop_back();
    }
}

bool Board::is_square_attacked(int sq, Side by) const {
    int f = file_of(sq);
    int r = rank_of(sq);
    int dir = (by == WHITE) ? 1 : -1;
    int pawn_rank = r - dir;
    if (pawn_rank >= 0 && pawn_rank <= 7) {
        int left = f - 1;
        int right = f + 1;
        if (left >= 0) {
            int from = make_sq(left, pawn_rank);
            int p = pos.board[from];
            if ((by == WHITE && p == WP) || (by == BLACK && p == BP)) return true;
        }
        if (right <= 7) {
            int from = make_sq(right, pawn_rank);
            int p = pos.board[from];
            if ((by == WHITE && p == WP) || (by == BLACK && p == BP)) return true;
        }
    }

    static const int knight_offsets[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
    for (auto &o : knight_offsets) {
        int nf = f + o[0];
        int nr = r + o[1];
        if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
        int p = pos.board[make_sq(nf, nr)];
        if ((by == WHITE && p == WN) || (by == BLACK && p == BN)) return true;
    }

    static const int bishop_dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto &d : bishop_dirs) {
        int nf = f + d[0];
        int nr = r + d[1];
        while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
            int p = pos.board[make_sq(nf, nr)];
            if (p != EMPTY) {
                if ((by == WHITE && (p == WB || p == WQ)) || (by == BLACK && (p == BB || p == BQ))) return true;
                break;
            }
            nf += d[0];
            nr += d[1];
        }
    }

    static const int rook_dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (auto &d : rook_dirs) {
        int nf = f + d[0];
        int nr = r + d[1];
        while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
            int p = pos.board[make_sq(nf, nr)];
            if (p != EMPTY) {
                if ((by == WHITE && (p == WR || p == WQ)) || (by == BLACK && (p == BR || p == BQ))) return true;
                break;
            }
            nf += d[0];
            nr += d[1];
        }
    }

    static const int king_dirs[8][2] = {{1,1},{1,0},{1,-1},{0,1},{0,-1},{-1,1},{-1,0},{-1,-1}};
    for (auto &d : king_dirs) {
        int nf = f + d[0];
        int nr = r + d[1];
        if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
        int p = pos.board[make_sq(nf, nr)];
        if ((by == WHITE && p == WK) || (by == BLACK && p == BK)) return true;
    }

    return false;
}

bool Board::in_check(Side side) const {
    int king_sq = -1;
    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if ((side == WHITE && p == WK) || (side == BLACK && p == BK)) {
            king_sq = sq;
            break;
        }
    }
    if (king_sq == -1) return false;
    return is_square_attacked(king_sq, opposite(side));
}

void Board::update_hash() {
    uint64_t h = 0;
    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p != EMPTY) h ^= keys.piece[sq][p];
    }
    if (pos.side_to_move == BLACK) h ^= keys.side;
    h ^= keys.castling[pos.castling_rights];
    int ep_index = pos.ep_square == -1 ? 8 : file_of(pos.ep_square);
    h ^= keys.ep[ep_index];
    pos.hash = h;
}

int Board::make_null_move() {
    int old_ep = pos.ep_square;
    pos.side_to_move = opposite(pos.side_to_move);
    pos.ep_square = -1;
    update_hash();
    hash_history.push_back(pos.hash);
    return old_ep;
}

void Board::unmake_null_move(int old_ep) {
    if (!hash_history.empty()) hash_history.pop_back();
    pos.side_to_move = opposite(pos.side_to_move);
    pos.ep_square = old_ep;
    update_hash();
}

bool Board::is_repetition() const {
    if (hash_history.empty()) return false;
    uint64_t h = pos.hash;
    int count = 0;
    for (auto v : hash_history) {
        if (v == h) count++;
        if (count >= 3) return true;
    }
    return false;
}

bool Board::is_insufficient_material() const {
    int w_knights = 0, w_bishops = 0, w_others = 0;
    int b_knights = 0, b_bishops = 0, b_others = 0;
    int w_bishop_sq = -1, b_bishop_sq = -1;
    for (int sq = 0; sq < 64; ++sq) {
        int p = pos.board[sq];
        if (p == EMPTY || p == WK || p == BK) continue;
        if (p == WN) { w_knights++; }
        else if (p == WB) { w_bishops++; w_bishop_sq = sq; }
        else if (is_white((Piece)p)) { w_others++; }
        else if (p == BN) { b_knights++; }
        else if (p == BB) { b_bishops++; b_bishop_sq = sq; }
        else if (is_black((Piece)p)) { b_others++; }
    }
    // Any pawns, rooks, or queens -> sufficient
    if (w_others > 0 || b_others > 0) return false;
    int w_minor = w_knights + w_bishops;
    int b_minor = b_knights + b_bishops;
    // K vs K
    if (w_minor == 0 && b_minor == 0) return true;
    // K+minor vs K
    if (w_minor == 0 && b_minor == 1) return true;
    if (w_minor == 1 && b_minor == 0) return true;
    // K+B vs K+B with same color square bishops
    if (w_bishops == 1 && b_bishops == 1 && w_knights == 0 && b_knights == 0) {
        bool w_light = ((file_of(w_bishop_sq) + rank_of(w_bishop_sq)) % 2) == 0;
        bool b_light = ((file_of(b_bishop_sq) + rank_of(b_bishop_sq)) % 2) == 0;
        if (w_light == b_light) return true;
    }
    return false;
}

} // namespace chess
