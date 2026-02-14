#include "engine.h"
#include "evaluation.h"
#include "movegen.h"

#include <iostream>
#include <sstream>

using namespace std;

namespace chess {

Engine::Engine() {
    init_pst_tables();
    board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void Engine::set_startpos() {
    board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    board.hash_history.clear();
    board.last_move_white = Move();
    board.last_move_black = Move();
}

void Engine::new_game() {
    set_startpos();
    searcher.new_game();
}

void Engine::set_position_from_uci(const string &cmd) {
    // Parse "position startpos moves e2e4 e7e5 ..."
    // or    "position fen ... moves ..."
    string rest = cmd;
    size_t pos_idx = rest.find("startpos");
    if (pos_idx != string::npos) {
        set_startpos();
        rest = rest.substr(pos_idx + 8);
    } else {
        size_t fen_idx = rest.find("fen ");
        if (fen_idx != string::npos) {
            string after = rest.substr(fen_idx + 4);
            size_t moves_idx = after.find(" moves ");
            string fen;
            if (moves_idx != string::npos) {
                fen = after.substr(0, moves_idx);
                rest = after.substr(moves_idx);
            } else {
                fen = after;
                rest = "";
            }
            board.parse_fen(fen);
            board.hash_history.clear();
            board.last_move_white = Move();
            board.last_move_black = Move();
        }
    }

    size_t moves_idx = rest.find("moves ");
    if (moves_idx != string::npos) {
        string moves_str = rest.substr(moves_idx + 6);
        istringstream iss(moves_str);
        string move_str;
        while (iss >> move_str) {
            apply_uci_move(move_str);
        }
    }
}

void Engine::apply_uci_move(const string &uci) {
    if (uci.size() < 4) return;
    int from = str_to_sq(uci.substr(0, 2));
    int to = str_to_sq(uci.substr(2, 2));
    int promo = EMPTY;
    if (uci.size() == 5) {
        promo = promo_from_char(uci[4], board.pos.side_to_move);
    }
    auto moves = generate_legal_moves(board);
    for (auto &m : moves) {
        if (m.from == from && m.to == to) {
            if (promo != EMPTY && m.promotion != promo) continue;
            board.make_move(m);

            return;
        }
    }
}

vector<string> Engine::legal_moves_uci() {
    vector<string> result;
    auto moves = generate_legal_moves(board);
    for (auto &m : moves) {
        result.push_back(move_to_uci(m));
    }
    return result;
}

Move Engine::search_bestmove(int max_depth, int time_limit_ms) {
    return searcher.search_bestmove(board, max_depth, time_limit_ms);
}

int Engine::compute_time_ms(int wtime, int btime, int winc, int binc) const {
    int our_time = (board.pos.side_to_move == WHITE) ? wtime : btime;
    int our_inc = (board.pos.side_to_move == WHITE) ? winc : binc;
    int base = our_time / 30 + our_inc / 2;
    if (base < 100) base = 100;
    if (base > our_time / 3) base = our_time / 3;
    return base;
}

string Engine::probe_book() const {
    return book.probe(board.pos.hash);
}

string Engine::move_to_uci_public(const Move &m) const {
    return move_to_uci(m);
}

bool Engine::has_legal_moves() {
    return !generate_legal_moves(board).empty();
}

bool Engine::check_insufficient_material() const {
    return board.is_insufficient_material();
}

} // namespace chess
