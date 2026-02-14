#include "book.h"
#include "position.h"
#include "movegen.h"

#include <random>
#include <chrono>
#include <sstream>

namespace chess {

OpeningBook::OpeningBook() {
    init_book();
}

std::string OpeningBook::probe(uint64_t hash) const {
    auto it = book_.find(hash);
    if (it == book_.end()) return "";

    const auto &entries = it->second;
    if (entries.empty()) return "";

    // Weighted random selection
    int total = 0;
    for (auto &e : entries) total += e.weight;

    static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> dist(0, total - 1);
    int r = dist(rng);

    int cumulative = 0;
    for (auto &e : entries) {
        cumulative += e.weight;
        if (r < cumulative) return e.move;
    }
    return entries.back().move;
}

bool OpeningBook::in_book(uint64_t hash) const {
    return book_.find(hash) != book_.end();
}

void OpeningBook::add_line(const std::string &moves, int weight) {
    Board board;
    board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    std::istringstream iss(moves);
    std::string uci_move;

    while (iss >> uci_move) {
        uint64_t hash = board.pos.hash;

        // Add this move as a book option for this position
        auto &entries = book_[hash];

        // Check if move already exists
        bool found = false;
        for (auto &e : entries) {
            if (e.move == uci_move) {
                e.weight += weight;
                found = true;
                break;
            }
        }
        if (!found) {
            entries.push_back({uci_move, weight});
        }

        // Make the move on the board
        int from = str_to_sq(uci_move.substr(0, 2));
        int to = str_to_sq(uci_move.substr(2, 2));
        int promo = EMPTY;
        if (uci_move.size() == 5) {
            promo = promo_from_char(uci_move[4], board.pos.side_to_move);
        }

        auto legal = generate_legal_moves(board);
        bool made = false;
        for (auto &m : legal) {
            if (m.from == from && m.to == to) {
                if (promo != EMPTY && m.promotion != promo) continue;
                board.make_move(m);
                made = true;
                break;
            }
        }
        if (!made) break; // invalid move in line
    }
}

void OpeningBook::init_book() {
    // ============================================================
    // OPEN GAMES (1.e4)
    // ============================================================

    // --- Italian Game / Giuoco Piano ---
    add_line("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4", 100);
    add_line("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 d2d3 g8f6 b1c3 d7d6 f3g5", 80);
    add_line("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 c2c3 d7d6 b2b4", 70);

    // --- Ruy Lopez ---
    add_line("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8", 120);
    add_line("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 d2d3", 100);
    add_line("e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 g8f6 d2d3", 80);
    add_line("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6 e1g1", 70);

    // --- Scotch Game ---
    add_line("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 b1c3 f8b4", 90);
    add_line("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 f8c5 b1c3 d8f6", 80);

    // --- King's Gambit ---
    add_line("e2e4 e7e5 f2f4 e5f4 g1f3 d7d6 d2d4 g7g5 h2h4 g5g4 f3e5", 70);
    add_line("e2e4 e7e5 f2f4 e5f4 g1f3 g7g5 f1c4 g5g4 e1g1", 60);

    // --- Petroff Defense ---
    add_line("e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5 f1d3", 90);
    add_line("e2e4 e7e5 g1f3 g8f6 d2d4 f6e4 f1d3 d7d5 f3e5", 80);

    // --- Philidor Defense ---
    add_line("e2e4 e7e5 g1f3 d7d6 d2d4 g8f6 b1c3 b8d7 f1c4", 70);

    // --- Four Knights ---
    add_line("e2e4 e7e5 g1f3 b8c6 b1c3 g8f6 f1b5 f8b4 e1g1 e8g8 d2d3", 80);

    // ============================================================
    // SICILIAN DEFENSE (1.e4 c5)
    // ============================================================

    // --- Open Sicilian ---
    add_line("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 f1e2 e7e5 d4b3", 110);
    add_line("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1g5 e7e6 f2f4", 100);
    // --- Najdorf ---
    add_line("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 f2f3 e7e5 d4b3", 90);
    // --- Dragon ---
    add_line("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 c1e3 f8g7 f2f3", 90);
    // --- Sveshnikov ---
    add_line("e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4b5 d7d6", 80);
    // --- Alapin ---
    add_line("e2e4 c7c5 c2c3 d7d5 e4d5 d8d5 d2d4 g8f6 g1f3", 80);
    // --- Closed Sicilian ---
    add_line("e2e4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 d2d3 d7d6", 70);

    // ============================================================
    // FRENCH DEFENSE (1.e4 e6)
    // ============================================================
    add_line("e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7 g5e7 d8e7", 90);
    add_line("e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 a2a3 b4c3 b2c3", 80);
    add_line("e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 b8c6 g1f3 d8b6", 80);
    add_line("e2e4 e7e6 d2d4 d7d5 b1d2 g8f6 e4e5 f6d7 f1d3 c7c5 c2c3", 70);

    // ============================================================
    // CARO-KANN (1.e4 c6)
    // ============================================================
    add_line("e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4 h7h6 g1f3", 90);
    add_line("e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3 e7e6 f1e2 b8d7 e1g1", 80);
    add_line("e2e4 c7c6 d2d4 d7d5 e4d5 c6d5 c2c4 g8f6 b1c3 e7e6 g1f3", 70);

    // ============================================================
    // PIRC / MODERN (1.e4 d6 / 1.e4 g6)
    // ============================================================
    add_line("e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 g1f3 f8g7 f1e2 e8g8 e1g1", 70);
    add_line("e2e4 g7g6 d2d4 f8g7 b1c3 d7d6 g1f3 g8f6 f1e2 e8g8 e1g1", 70);

    // ============================================================
    // CLOSED GAMES (1.d4)
    // ============================================================

    // --- Queen's Gambit Declined ---
    add_line("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 b8d7 a1c1", 110);
    add_line("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 f8e7 c1f4 e8g8 e2e3", 90);

    // --- Queen's Gambit Accepted ---
    add_line("d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5 e1g1", 90);

    // --- Slav Defense ---
    add_line("d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5 e2e3 e7e6", 90);
    add_line("d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 e2e3 c8f5 b1c3 e7e6 f1d3", 80);

    // --- King's Indian Defense ---
    add_line("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1", 100);
    add_line("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f3 e8g8 c1e3", 90);

    // --- Nimzo-Indian ---
    add_line("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 d1c2 e8g8 a2a3 b4c3 c2c3 b7b6", 100);
    add_line("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5 g1f3", 90);

    // --- Queen's Indian ---
    add_line("d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7 e1g1 e8g8", 90);

    // --- Grünfeld ---
    add_line("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3 b2c3 f8g7 g1f3", 90);

    // --- Benoni ---
    add_line("d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 e2e4 g7g6", 70);

    // --- Dutch Defense ---
    add_line("d2d4 f7f5 g2g3 g8f6 f1g2 e7e6 g1f3 f8e7 e1g1 e8g8 c2c4", 70);

    // ============================================================
    // ENGLISH OPENING (1.c4)
    // ============================================================
    add_line("c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 d7d5 c4d5 f6d5 f1g2", 80);
    add_line("c2c4 g8f6 b1c3 e7e5 g1f3 b8c6 g2g3 f8b4 f1g2 e8g8 e1g1", 70);
    add_line("c2c4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 g1f3", 70);

    // ============================================================
    // RETI OPENING (1.Nf3)
    // ============================================================
    add_line("g1f3 d7d5 g2g3 g8f6 f1g2 g7g6 e1g1 f8g7 d2d3 e8g8 b1d2", 80);
    add_line("g1f3 d7d5 c2c4 e7e6 g2g3 g8f6 f1g2 f8e7 e1g1 e8g8", 70);

    // ============================================================
    // LONDON SYSTEM
    // ============================================================
    add_line("d2d4 d7d5 c1f4 g8f6 e2e3 e7e6 g1f3 f8d6 f4d6 c7d6 b1d2 e8g8", 80);
    add_line("d2d4 g8f6 c1f4 d7d5 e2e3 e7e6 g1f3 f8d6 f1d3 e8g8 e1g1", 80);

    // ============================================================
    // BLACK RESPONSES — adding weight to common Black moves
    // ============================================================

    // 1.e4 — Black responses
    add_line("e2e4 e7e5", 120);  // open game
    add_line("e2e4 c7c5", 110);  // sicilian
    add_line("e2e4 e7e6", 80);   // french
    add_line("e2e4 c7c6", 70);   // caro-kann
    add_line("e2e4 d7d5", 60);   // scandinavian
    add_line("e2e4 g8f6", 50);   // alekhine

    // 1.d4 — Black responses
    add_line("d2d4 d7d5", 110);
    add_line("d2d4 g8f6", 110);
    add_line("d2d4 e7e6", 70);
    add_line("d2d4 f7f5", 50);   // dutch

    // 1.Nf3 — Black responses
    add_line("g1f3 d7d5", 90);
    add_line("g1f3 g8f6", 80);

    // 1.c4 — Black responses
    add_line("c2c4 e7e5", 80);
    add_line("c2c4 g8f6", 70);
    add_line("c2c4 c7c5", 60);
}

} // namespace chess
