#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace chess {

enum Piece {
    EMPTY = 0,
    WP, WN, WB, WR, WQ, WK,
    BP, BN, BB, BR, BQ, BK
};

enum Side { WHITE = 0, BLACK = 1 };

template <typename T>
inline T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline bool is_white(Piece p) { return p >= WP && p <= WK; }
inline bool is_black(Piece p) { return p >= BP && p <= BK; }
inline int piece_type(Piece p) {
    if (p == EMPTY) return 0;
    if (p <= WK) return p;
    return p - 6;
}

inline int file_of(int sq) { return sq & 7; }
inline int rank_of(int sq) { return sq >> 3; }

inline int make_sq(int file, int rank) { return rank * 8 + file; }

inline string sq_to_str(int sq) {
    string s;
    s.push_back(char('a' + file_of(sq)));
    s.push_back(char('1' + rank_of(sq)));
    return s;
}

inline int str_to_sq(const string &s) {
    if (s.size() < 2) return -1;
    int f = s[0] - 'a';
    int r = s[1] - '1';
    if (f < 0 || f > 7 || r < 0 || r > 7) return -1;
    return make_sq(f, r);
}

struct Move {
    int from = -1;
    int to = -1;
    int promotion = EMPTY;
    bool is_en_passant = false;
    bool is_castle = false;
    int captured = EMPTY;
    int moved = EMPTY;
};

struct Undo {
    int castling = 0;
    int ep_square = -1;
    int halfmove = 0;
    int fullmove = 1;
    int captured = EMPTY;
    Move last_move_white{};
    Move last_move_black{};
};

struct Position {
    array<int, 64> board{};
    Side side_to_move = WHITE;
    int castling_rights = 0; // 1 WK,2 WQ,4 BK,8 BQ
    int ep_square = -1;
    int halfmove_clock = 0;
    int fullmove_number = 1;
    uint64_t hash = 0;

    Position() { board.fill(EMPTY); }
};

struct HashKeys {
    array<array<uint64_t, 13>, 64> piece{};
    uint64_t side = 0;
    array<uint64_t, 16> castling{};
    array<uint64_t, 9> ep{}; // file or 8 for no ep

    HashKeys() {
        mt19937_64 rng(20260129);
        auto rand64 = [&]() { return rng(); };
        for (int sq = 0; sq < 64; ++sq) {
            for (int p = 0; p < 13; ++p) {
                piece[sq][p] = rand64();
            }
        }
        side = rand64();
        for (auto &v : castling) v = rand64();
        for (auto &v : ep) v = rand64();
    }
};

struct TTEntry {
    int depth = 0;
    int score = 0;
    Move best;
};

class Engine {
public:
    Engine() {
        init_tables();
        set_startpos();
    }

    void set_startpos() {
        parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    }
    
    void new_game() {
        set_startpos();
        tt.clear(); // Clear transposition table
        cerr << "info string New game started, TT cleared" << endl;
    }

    void parse_fen(const string &fen) {
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

    string to_fen() const {
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

    void set_position_from_uci(const string &line) {
        // line: position startpos moves ... OR position fen ... moves ...
        stringstream ss(line);
        string token;
        ss >> token; // "position"
        ss >> token;
        if (token == "startpos") {
            set_startpos();
        } else if (token == "fen") {
            string fen, part;
            for (int i = 0; i < 6; ++i) {
                ss >> part;
                if (!fen.empty()) fen += ' ';
                fen += part;
            }
            parse_fen(fen);
        }
        if (ss >> token) {
            if (token == "moves") {
                string mv;
                while (ss >> mv) {
                    apply_uci_move(mv);
                }
            }
        }
    }

    void apply_uci_move(const string &mv) {
        if (mv.size() < 4) return;
        int from = str_to_sq(mv.substr(0, 2));
        int to = str_to_sq(mv.substr(2, 2));
        int promo = EMPTY;
        if (mv.size() >= 5) promo = promo_from_char(mv[4], pos.side_to_move);

        Move move;
        move.from = from;
        move.to = to;
        move.promotion = promo;
        auto legal = generate_legal_moves();
        for (auto &m : legal) {
            if (m.from == from && m.to == to && (m.promotion == promo || (m.promotion == EMPTY && promo == EMPTY))) {
                make_move(m);
                return;
            }
        }
    }

    vector<Move> generate_legal_moves() {
        vector<Move> moves;
        generate_pseudo_moves(moves);
        vector<Move> legal;
        for (auto &m : moves) {
            Undo u = make_move(m);
            if (!in_check(opposite(pos.side_to_move))) {
                legal.push_back(m);
            }
            unmake_move(m, u);
        }
        return legal;
    }

    string legal_moves_uci() {
        auto moves = generate_legal_moves();
        string out = "legal";
        for (auto &m : moves) {
            out += ' ';
            out += move_to_uci(m);
        }
        return out;
    }

    Move search_bestmove(int max_depth, int time_limit_ms) {
        stop_search = false;
        search_start = chrono::steady_clock::now();
        time_limit = time_limit_ms;
        Move best;
        int best_score = -INF;
        int depth_reached = 0;
        
        // DEBUG
        auto dbg_moves = generate_legal_moves();
        
        // Count pieces to decide if mate search is worthwhile
        int heavy_pieces = 0;
        for (int sq = 0; sq < 64; ++sq) {
            int p = pos.board[sq];
            if (p == WQ || p == BQ || p == WR || p == BR) heavy_pieces++;
        }
        
        // === QUICK CHECK: Do we have a forced mate in 3? ===
        // Only check if there are heavy pieces (otherwise mate unlikely in 3 moves)
        int our_mate = 0;
        if (heavy_pieces > 0) {
            our_mate = has_mate_in_n(3); // Check for mate in 3 (fast with node limit)
        }
        if (our_mate >= MATE_SCORE - 20) {
            // Find the SHORTEST mating move
            auto moves = generate_legal_moves();
            
            Move best_mate_move;
            int best_mate_score = 0;
            
            for (auto &m : moves) {
                Undo u = make_move(m);
                bool gives_check = in_check(pos.side_to_move);
                
                if (gives_check) {
                    // Check for immediate checkmate (opponent has no moves)
                    auto opp_moves = generate_legal_moves();
                    if (opp_moves.empty()) {
                        // This is CHECKMATE IN 1 - best possible!
                        unmake_move(m, u);
                        return m;
                    }
                    
                    // Otherwise check mate_search for longer mates
                    mate_search_nodes = 0;
                    int score = mate_search(5, false);
                    if (score > best_mate_score) {
                        best_mate_score = score;
                        best_mate_move = m;
                    }
                }
                unmake_move(m, u);
            }
            
            if (best_mate_score >= MATE_SCORE - 20) {
                return best_mate_move;
            }
        }
        
        // === QUICK CHECK: Obvious promotion to queen ===
        // If we have a pawn that can promote to queen safely, prioritize it!
        {
            auto moves = generate_legal_moves();
            Move best_promo;
            for (auto &m : moves) {
                if (m.promotion == WQ || m.promotion == BQ) {
                    // Check if promotion is safe (not immediately captured)
                    Undo u = make_move(m);
                    bool queen_safe = !is_square_attacked(m.to, pos.side_to_move);
                    unmake_move(m, u);
                    if (queen_safe) {
                        best_promo = m;
                        break; // Take first safe queen promotion
                    }
                }
            }
            if (best_promo.from != -1) {
                return best_promo;
            }
        }
        
        // Main iterative deepening search
        node_count = 0;
        Move prev_best; // Best from completed iteration
        for (int depth = 1; depth <= max_depth; ++depth) {
            Move curr_best;
            int score = alpha_beta(depth, -INF, INF, curr_best);
            if (stop_search) {
                // Use best from previous completed iteration
                if (prev_best.from != -1) {
                    best = prev_best;
                } else if (curr_best.from != -1) {
                    best = curr_best; // Fallback to partial if no completed
                }
                break;
            }
            // Iteration completed - save as best
            best = curr_best;
            prev_best = curr_best;
            best_score = score;
            depth_reached = depth;
        }
        
        // === SAFETY CHECK: Does our best move allow opponent to mate us? ===
        if (best.from != -1 && !stop_search) {
            Undo u = make_move(best);
            bool allows_mate1 = opponent_has_mate_in_one();
            int opp_mate = 0;
            if (!allows_mate1) {
                opp_mate = opponent_has_mate_in_n(2); // Quick check: mate in 2
            }
            
            if (allows_mate1 || opp_mate >= MATE_SCORE - 20) {
                // This move allows mate! Find alternative
                unmake_move(best, u);
                cerr << "info string WARNING: Best move allows mate, finding alternative" << endl;
                
                auto moves = generate_legal_moves();
                Move safe_best;
                int safe_score = -INF;
                
                for (auto &m : moves) {
                    Undo u2 = make_move(m);
                    bool m_allows_mate1 = opponent_has_mate_in_one();
                    int m_opp_mate = m_allows_mate1 ? MATE_SCORE : opponent_has_mate_in_n(2);
                    
                    int score;
                    if (m_allows_mate1 || m_opp_mate >= MATE_SCORE - 20) {
                        score = -MATE_SCORE; // Skip moves that allow mate
                    } else {
                        Move child_best;
                        score = -alpha_beta(3, -INF, INF, child_best);
                    }
                    unmake_move(m, u2);
                    
                    if (score > safe_score) {
                        safe_score = score;
                        safe_best = m;
                    }
                }
                
                if (safe_best.from != -1) {
                    best = safe_best;
                }
            } else {
                unmake_move(best, u);
            }
        }
        
        if (best.from != -1 && !stop_search) {
            if (is_mate_trap(best, MATE_TRAP_PLIES)) {
                auto moves = generate_legal_moves();
                Move alt_best = best;
                int alt_score = -INF;
                int alt_depth = max(1, min(3, depth_reached - 1));
                for (auto &m : moves) {
                    if (time_up()) break;
                    if (is_mate_trap(m, MATE_TRAP_PLIES)) continue;
                    Undo u = make_move(m);
                    Move child_best;
                    int score = -alpha_beta(alt_depth, -INF, INF, child_best);
                    unmake_move(m, u);
                    if (score > alt_score) {
                        alt_score = score;
                        alt_best = m;
                    }
                }
                if (alt_score > -INF / 2) {
                    best = alt_best;
                }
            }
        }
        if (best.from == -1) {
            auto moves = generate_legal_moves();
            if (!moves.empty()) {
                best = moves[0];
            }
        }
        return best;
    }

    int compute_time_ms(int wtime, int btime, int winc, int binc) const {
        int remaining = pos.side_to_move == WHITE ? wtime : btime;
        int inc = pos.side_to_move == WHITE ? winc : binc;
        int t = remaining / 30 + inc / 2;
        return clamp(t, 50, 5000);
    }

    string move_to_uci_public(const Move &m) const { return move_to_uci(m); }

    bool has_legal_moves() { return !generate_legal_moves().empty(); }

private:
    Position pos;
    HashKeys keys;
    unordered_map<uint64_t, TTEntry> tt;
    vector<uint64_t> hash_history;
    chrono::steady_clock::time_point search_start;
    int time_limit = 1000;
    bool stop_search = false;

    static constexpr int INF = 1000000;
    static constexpr int MATE_SCORE = 900000;
    static constexpr int MATE_TRAP_PLIES = 6;
    static constexpr int MATE_PROBE_LIMIT = 8000;
    int mate_probe_nodes = 0;
    Move last_move_white{};
    Move last_move_black{};

    array<int, 64> pst_pawn{};
    array<int, 64> pst_knight{};
    array<int, 64> pst_bishop{};
    array<int, 64> pst_rook{};
    array<int, 64> pst_queen{};
    array<int, 64> pst_king{};

    int piece_value(int p) const {
        switch (piece_type((Piece)p)) {
            case WP: return 100;
            case WN: return 320;
            case WB: return 330;
            case WR: return 500;
            case WQ: return 900;
            case WK: return 20000;
            default: return 0;
        }
    }

    int move_score(const Move &m) const {
        int score = 0;
        
        // === CRITICAL: Check if we can capture enemy QUEEN ===
        // This should have highest priority!
        {
            int cap_pt = piece_type((Piece)m.captured);
            if (cap_pt == WQ) {
                // We can take the queen!
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                Side opp = (mover == WHITE) ? BLACK : WHITE;
                int our_value = piece_value(m.moved);
                
                if (!is_square_attacked(m.to, opp)) {
                    // Free queen capture!
                    score += 5000; // Massive bonus
                } else {
                    // Queen is defended, but trading for queen is almost always good
                    if (our_value < 900) {
                        score += 3000; // Trading minor/rook for queen = great
                    }
                }
            }
        }
        
        // === CRITICAL: Check if any of OUR pieces is hanging (under attack, not defended) ===
        // Also check if attacked by LESS valuable piece (bad trade even if defended)
        {
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;
            
            // Find our piece under threat (hanging OR attacked by less valuable piece)
            int worst_threat_sq = -1;
            int worst_threat_value = 0;
            
            for (int sq = 0; sq < 64; ++sq) {
                int p = pos.board[sq];
                if (p == EMPTY) continue;
                bool our_piece = (mover == WHITE) ? is_white((Piece)p) : is_black((Piece)p);
                if (!our_piece) continue;
                if (piece_type((Piece)p) == WK) continue; // Skip king
                
                int our_val = piece_value(p);
                
                // Is this piece attacked?
                if (is_square_attacked(sq, opp)) {
                    // Find least valuable attacker
                    int min_attacker_val = 10000;
                    for (int asq = 0; asq < 64; ++asq) {
                        int ap = pos.board[asq];
                        if (ap == EMPTY) continue;
                        bool enemy = (opp == WHITE) ? is_white((Piece)ap) : is_black((Piece)ap);
                        if (!enemy) continue;
                        
                        // Check if this enemy attacks sq
                        // Simple check: is sq attacked by opp - we know it is
                        // Need to find the actual attacker value
                        // For simplicity, check pawn attacks
                        int apf = file_of(asq);
                        int apr = rank_of(asq);
                        int sf = file_of(sq);
                        int sr = rank_of(sq);
                        int pt_a = piece_type((Piece)ap);
                        
                        if (pt_a == WP) {
                            // Pawn attack check
                            if (opp == WHITE && apr + 1 == sr && abs(apf - sf) == 1) {
                                min_attacker_val = 100;
                            }
                            if (opp == BLACK && apr - 1 == sr && abs(apf - sf) == 1) {
                                min_attacker_val = 100;
                            }
                        }
                    }
                    
                    bool is_defended = is_square_attacked(sq, mover);
                    
                    // Piece is threatened if:
                    // 1. Not defended (hanging) OR
                    // 2. Attacked by less valuable piece (bad trade)
                    bool is_threatened = !is_defended || (min_attacker_val < our_val - 50);
                    
                    if (is_threatened) {
                        int threat_value = is_defended ? (our_val - min_attacker_val) : our_val;
                        if (threat_value > worst_threat_value) {
                            worst_threat_value = threat_value;
                            worst_threat_sq = sq;
                        }
                    }
                }
            }
            
            if (worst_threat_sq != -1) {
                // We have a piece under threat!
                
                // This move should either:
                // 1. Move the threatened piece
                // 2. Capture the attacker
                // 3. Block the attack
                
                bool saves_piece = (m.from == worst_threat_sq);
                
                if (saves_piece) {
                    // Check if new square is safe
                    if (!is_square_attacked(m.to, opp)) {
                        score += worst_threat_value * 3; // Saving the piece!
                    } else {
                        score += worst_threat_value; // Moving but still attacked
                    }
                } else if (m.captured == EMPTY) {
                    // Ignoring our threatened piece!
                    score -= worst_threat_value * 3; // Huge penalty
                }
            }
        }
        
        // === CRITICAL: Check if our queen is under attack ===
        // If so, give HUGE bonus to queen moves that escape
        {
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;
            int our_queen = (mover == WHITE) ? WQ : BQ;
            int our_queen_sq = -1;
            
            // Find our queen
            for (int sq = 0; sq < 64; ++sq) {
                if (pos.board[sq] == our_queen) {
                    our_queen_sq = sq;
                    break;
                }
            }
            
            if (our_queen_sq != -1 && is_square_attacked(our_queen_sq, opp)) {
                // Our queen is under attack!
                int pt = piece_type((Piece)m.moved);
                
                if (pt == WQ) {
                    // This move IS a queen move - check if it escapes
                    if (!is_square_attacked(m.to, opp)) {
                        score += 2000; // HUGE bonus for saving the queen
                    } else {
                        // Queen moving to another attacked square - still bad but better than nothing
                        score += 500;
                    }
                    // Bonus if queen captures the attacker
                    if (m.captured != EMPTY) {
                        score += 800; // Taking the attacker is also good
                    }
                } else {
                    // Not a queen move while queen is attacked = very bad
                    // Unless this move blocks or captures the attacker
                    if (m.captured == EMPTY) {
                        score -= 1500; // Penalty for ignoring queen attack
                    }
                }
            }
            
            // === CRITICAL: Queen moving to an attacked square = SUICIDE ===
            int pt_queen = piece_type((Piece)m.moved);
            if (pt_queen == WQ && m.captured == EMPTY) {
                // Queen moving without capturing - check destination
                if (is_square_attacked(m.to, opp)) {
                    // Queen going to attacked square = losing queen for free!
                    score -= 2000; // Massive penalty
                }
            }
        }
        
        // === CRITICAL: ANY piece moving to attacked undefended square ===
        {
            int pt = piece_type((Piece)m.moved);
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;
            
            if (m.captured == EMPTY && is_square_attacked(m.to, opp)) {
                // Moving to attacked square
                if (!is_square_attacked(m.to, mover)) {
                    // And it's NOT defended - we lose the piece!
                    if (pt == WQ) {
                        score -= 2500; // Losing queen = disaster
                    } else if (pt == WR) {
                        score -= 1000; // Losing rook
                    } else if (pt == WB || pt == WN) {
                        score -= 600; // Losing minor piece
                    }
                }
            }
        }
        
        if (m.captured != EMPTY) {
            // === CRITICAL FIX: Don't give huge bonus for captures that lose material ===
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;
            
            int our_value = piece_value(m.moved);
            int their_value = piece_value(m.captured);
            bool is_defended = is_square_attacked(m.to, opp);
            
            if (!is_defended) {
                // Free capture - use normal MVV-LVA
                score += 10 * their_value - our_value;
            } else {
                // Target is defended - calculate if trade is good
                if (our_value <= their_value + 50) {
                    // Good or equal trade (e.g., knight takes bishop)
                    score += their_value - our_value + 100;
                } else {
                    // BAD trade - we lose material!
                    // e.g., Queen takes knight, pawn recaptures = we lose 580
                    int material_loss = our_value - their_value;
                    score -= material_loss * 2; // Heavy penalty
                }
            }
        }
        if (m.promotion != EMPTY) {
            score += piece_value(m.promotion) + 800;
        }
        if (m.is_castle) score += 350; // HUGE bonus for castling - king safety is paramount
        
        // === CRITICAL: If castling is available, penalize other king moves ===
        {
            int pt = piece_type((Piece)m.moved);
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            if (pt == WK && !m.is_castle) {
                // King move that is NOT castling
                bool can_castle_k = (mover == WHITE) ? (pos.castling_rights & 1) : (pos.castling_rights & 4);
                bool can_castle_q = (mover == WHITE) ? (pos.castling_rights & 2) : (pos.castling_rights & 8);
                if (can_castle_k || can_castle_q) {
                    score -= 300; // Losing castling rights is terrible!
                }
            }
        }
        
        if (is_reverse_of_last(m)) {
            score -= reverse_move_penalty(m);
        }
        if (is_repeat_piece_move(m)) {
            // Penalize moving the same piece again (wastes tempo)
            int pt = piece_type((Piece)m.moved);
            int penalty = 150; // Base penalty
            if (pt == WB) penalty = 200; // Bishops shuffling is particularly bad (Bd6-Bf4-Bd6)
            if (pt == WN) penalty = 180; // Knights too
            if (pt == WR) penalty = 220; // Rooks wandering aimlessly
            score -= penalty;
        }
        
        // === CRITICAL: Bonus for developing knights and bishops ===
        {
            int pt = piece_type((Piece)m.moved);
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            int to_rank = rank_of(m.to);
            int to_file = file_of(m.to);
            
            if (pos.fullmove_number <= 12 && m.captured == EMPTY) {
                // Knight development bonus
                if (pt == WN) {
                    // Knight to f3/c3 for white or f6/c6 for black
                    if (mover == WHITE && to_rank == 2) {
                        if (to_file == 2 || to_file == 5) score += 80; // c3 or f3
                    }
                    if (mover == BLACK && to_rank == 5) {
                        if (to_file == 2 || to_file == 5) score += 80; // c6 or f6
                    }
                    // Knights controlling center
                    if (to_file >= 2 && to_file <= 5 && to_rank >= 2 && to_rank <= 5) {
                        score += 40; // Central control
                    }
                }
                
                // Bishop development bonus
                if (pt == WB) {
                    // Active diagonals (not edge files)
                    if (to_file >= 1 && to_file <= 6 && to_rank >= 1 && to_rank <= 6) {
                        score += 50;
                    }
                    // Long diagonals (fianchetto or active squares)
                    if ((to_file == 1 && to_rank == 1) || (to_file == 6 && to_rank == 1) ||
                        (to_file == 1 && to_rank == 6) || (to_file == 6 && to_rank == 6)) {
                        score += 30; // Fianchetto positions
                    }
                    // Bc4/Bc5/Bb5/Be2 etc - active squares
                    if ((to_file >= 2 && to_file <= 5) && (to_rank >= 2 && to_rank <= 5)) {
                        score += 40; // Central bishop is good
                    }
                }
                
                // Central pawn moves are good
                if (pt == WP) {
                    if (to_file == 3 || to_file == 4) { // d or e file
                        if ((mover == WHITE && (to_rank == 2 || to_rank == 3)) ||
                            (mover == BLACK && (to_rank == 4 || to_rank == 5))) {
                            score += 60; // e4, d4, e5, d5 are great
                        }
                    }
                }
            }
        }
        
        // CRITICAL: Penalize pointless rook moves in opening
        // Rook moving to b1/g1/b8/g8 without capturing = very bad
        int pt = piece_type((Piece)m.moved);
        if (pt == WR && m.captured == EMPTY && pos.fullmove_number <= 15) {
            int to_file = file_of(m.to);
            int to_rank = rank_of(m.to);
            // Rook on back rank moving sideways = usually pointless
            if (to_rank == 0 || to_rank == 7) {
                // Penalize b1, c1, g1 (and mirror for black)
                if (to_file == 1 || to_file == 2 || to_file == 5 || to_file == 6) {
                    score -= 200;
                }
                // Any rook move on first rank without capture in early game
                if (pos.fullmove_number <= 10) {
                    score -= 150;
                }
            }
        }
        
        // Discourage moving pieces into attacked squares (non-captures)
        if (m.moved != EMPTY && m.captured == EMPTY) {
            Side mover = (m.moved <= WK) ? WHITE : BLACK;
            Side opp = (mover == WHITE) ? BLACK : WHITE;
            if (is_square_attacked(m.to, opp)) {
                // Only penalize if not defended
                if (!is_square_attacked(m.to, mover)) {
                    score -= piece_value(m.moved); // Full value penalty for hanging piece
                } else {
                    score -= piece_value(m.moved) / 4; // Smaller penalty if defended
                }
            }
        }
        
        // === ANTI-GREED: Penalize grabbing distant pawns in opening ===
        if (pos.fullmove_number <= 12 && m.captured != EMPTY) {
            int pt = piece_type((Piece)m.moved);
            int cap_pt = piece_type((Piece)m.captured);
            
            // Bishop or knight grabbing rook pawns (a or h file) = usually bad
            if ((pt == WB || pt == WN) && cap_pt == WP) {
                int to_file = file_of(m.to);
                if (to_file == 0 || to_file == 7) {
                    score -= 150; // Grabbing rook pawn loses tempo
                }
                // Especially bad: bishop going to a2/a7/h2/h7
                if (pt == WB) {
                    int to_rank = rank_of(m.to);
                    if ((to_file == 0 && (to_rank == 1 || to_rank == 6)) ||
                        (to_file == 7 && (to_rank == 1 || to_rank == 6))) {
                        score -= 100; // Bishop trapped potential
                    }
                }
            }
        }
        
        // === CRITICAL: Penalize flank pawn moves in opening ===
        // Moving a/b/g/h pawns early wastes tempo and doesn't develop
        {
            int pt = piece_type((Piece)m.moved);
            if (pt == WP && m.captured == EMPTY && pos.fullmove_number <= 10) {
                int from_file = file_of(m.from);
                
                // a-pawn and h-pawn moves are almost always bad in opening
                if (from_file == 0 || from_file == 7) {
                    score -= 180; // Heavy penalty for a/h pawn pushes
                }
                // b-pawn moves without fianchetto purpose
                if (from_file == 1) {
                    int to_rank = rank_of(m.to);
                    // b6 or b3 could be fianchetto prep, but b5/b4 is usually bad
                    Side mover = (m.moved <= WK) ? WHITE : BLACK;
                    if (mover == WHITE && to_rank == 3) {
                        // b4 for white = bad (not b3)
                        score -= 120;
                    } else if (mover == BLACK && to_rank == 4) {
                        // b5 for black = bad (not b6)
                        score -= 120;
                    }
                }
                // g-pawn moves can weaken king
                if (from_file == 6 && pos.fullmove_number <= 8) {
                    score -= 100; // g-pawn weakens kingside castle
                }
            }
        }
        
        // === CRITICAL: Pawn moves in front of castled king weaken it ===
        // g6, h6, f6 when king is castled kingside = dangerous
        {
            int pt = piece_type((Piece)m.moved);
            if (pt == WP && m.captured == EMPTY) {
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                Side opp = (mover == WHITE) ? BLACK : WHITE;
                int from_file = file_of(m.from);
                
                // Find our king
                int our_king = (mover == WHITE) ? WK : BK;
                int king_sq = -1;
                for (int sq = 0; sq < 64; ++sq) {
                    if (pos.board[sq] == our_king) {
                        king_sq = sq;
                        break;
                    }
                }
                
                if (king_sq != -1) {
                    int king_file = file_of(king_sq);
                    int king_rank = rank_of(king_sq);
                    
                    // King is castled kingside (on g or h file, back rank)
                    bool kingside_castled = (mover == WHITE) 
                        ? (king_file >= 6 && king_rank == 0)
                        : (king_file >= 6 && king_rank == 7);
                    
                    // King is castled queenside
                    bool queenside_castled = (mover == WHITE)
                        ? (king_file <= 2 && king_rank == 0)
                        : (king_file <= 2 && king_rank == 7);
                    
                    // Check if enemy queen is active (not on starting square)
                    int opp_queen = (opp == WHITE) ? WQ : BQ;
                    bool enemy_queen_active = false;
                    for (int sq = 0; sq < 64; ++sq) {
                        if (pos.board[sq] == opp_queen) {
                            int qr = rank_of(sq);
                            // Queen is active if not on back rank or if advanced
                            if ((opp == WHITE && qr >= 2) || (opp == BLACK && qr <= 5)) {
                                enemy_queen_active = true;
                            }
                            break;
                        }
                    }
                    
                    if (kingside_castled && enemy_queen_active) {
                        // Moving g or h pawn = weakening king!
                        if (from_file == 6 || from_file == 7) {
                            score -= 300; // Big penalty - opens king to attack
                        }
                        // Moving f pawn also weakens
                        if (from_file == 5) {
                            score -= 200;
                        }
                    }
                    
                    if (queenside_castled && enemy_queen_active) {
                        // Moving a, b, c pawn weakens queenside castle
                        if (from_file <= 2) {
                            score -= 250;
                        }
                    }
                }
            }
        }
        
        // === CRITICAL: Queen grabbing flank pawns is usually a TRAP ===
        {
            int pt = piece_type((Piece)m.moved);
            if (pt == WQ && m.captured != EMPTY) {
                int cap_pt = piece_type((Piece)m.captured);
                if (cap_pt == WP) {
                    int to_file = file_of(m.to);
                    int to_rank = rank_of(m.to);
                    Side mover = (m.moved <= WK) ? WHITE : BLACK;
                    
                    // Queen taking pawn on a/b file = very suspicious
                    if (to_file == 0 || to_file == 1) {
                        score -= 200; // Penalty for grabbing a/b pawn with queen
                        
                        // Extra penalty if queen goes to enemy's back rank area
                        if ((mover == WHITE && to_rank >= 5) || 
                            (mover == BLACK && to_rank <= 2)) {
                            score -= 150; // Queen deep in enemy territory = danger
                        }
                    }
                    // Same for g/h file
                    if (to_file == 6 || to_file == 7) {
                        score -= 200;
                        if ((mover == WHITE && to_rank >= 5) || 
                            (mover == BLACK && to_rank <= 2)) {
                            score -= 150;
                        }
                    }
                    
                    // Check if bishop can attack this square (diagonal threat)
                    // This catches Qxb2 type traps where Bd4 attacks the queen
                    int opp_bishop = (mover == WHITE) ? BB : WB;
                    for (int sq = 0; sq < 64; ++sq) {
                        if (pos.board[sq] == opp_bishop) {
                            // Check if bishop can reach m.to in one move (after moving)
                            int bf = file_of(sq);
                            int br = rank_of(sq);
                            int tf = to_file;
                            int tr = to_rank;
                            // Bishop moves diagonally
                            if (abs(bf - tf) == abs(br - tr) && abs(bf - tf) <= 3) {
                                // Bishop could potentially attack this square
                                score -= 250; // Huge penalty - possible trap!
                            }
                        }
                    }
                }
            }
        }
        
        // === CRITICAL: Never trade queen for minor piece ===
        {
            int pt2 = piece_type((Piece)m.moved);
            if (pt2 == WQ && m.captured != EMPTY) {
                int cap_pt = piece_type((Piece)m.captured);
                // Queen capturing bishop, knight or pawn
                if (cap_pt == WB || cap_pt == WN || cap_pt == WP) {
                    // Check if queen will be captured after this
                    Side mover = (m.moved <= WK) ? WHITE : BLACK;
                    Side opp = (mover == WHITE) ? BLACK : WHITE;
                    if (is_square_attacked(m.to, opp)) {
                        // We're trading queen for minor piece = CATASTROPHIC
                        // Must be larger than MVV-LVA bonus (10 * captured_value)
                        int captured_value = piece_value(m.captured);
                        int queen_value = 900;
                        // Net loss = queen - captured piece
                        int net_loss = queen_value - captured_value;
                        // Cancel out MVV-LVA bonus and add huge penalty
                        score -= (10 * captured_value); // Cancel MVV-LVA
                        score -= net_loss * 3; // Triple the material loss as penalty
                    }
                }
                // Queen capturing rook while attacked = also bad (trading 900 for 500)
                if (cap_pt == WR) {
                    Side mover = (m.moved <= WK) ? WHITE : BLACK;
                    Side opp = (mover == WHITE) ? BLACK : WHITE;
                    if (is_square_attacked(m.to, opp)) {
                        score -= 800; // Losing queen for rook is bad
                    }
                }
            }
            
            // === CRITICAL: Never move king to center of board ===
            if (pt2 == WK) {
                int to_rank = rank_of(m.to);
                int to_file = file_of(m.to);
                Side mover = (m.moved <= WK) ? WHITE : BLACK;
                Side opp = (mover == WHITE) ? BLACK : WHITE;
                
                // === HUGE penalty for king moving to center when in check ===
                // This is Ke7 instead of blocking with a piece - almost always bad
                if (is_square_attacked(m.from, opp)) { // King was in check (approximation)
                    // We're in check and moving king
                    if ((mover == WHITE && to_rank >= 1) || (mover == BLACK && to_rank <= 6)) {
                        // King moving forward (toward center) while in check
                        score -= 600; // Prefer blocking or capturing
                    }
                    // Extra penalty for going to exposed files (e, d especially)
                    if (to_file >= 3 && to_file <= 4) {
                        score -= 400; // King to d or e file while in check = very bad
                    }
                }
                
                // King moving to ranks 2-5 (center) = disaster
                if (to_rank >= 2 && to_rank <= 5) {
                    score -= 500; // Massive penalty
                    if (to_file >= 2 && to_file <= 5) {
                        score -= 300; // Even worse - center files
                    }
                }
                // King moving without castling in opening
                if (!m.is_castle && pos.fullmove_number <= 15) {
                    int from_rank = rank_of(m.from);
                    // King leaving back rank (not castling)
                    if ((from_rank == 0 && to_rank > 0) || (from_rank == 7 && to_rank < 7)) {
                        score -= 400;
                    }
                }
                
                // === CRITICAL: Penalize king moves that expose to attack ===
                // After castling, king should stay safe - don't walk into open files
                
                // Check if destination is attacked or on open file
                if (is_square_attacked(m.to, opp)) {
                    score -= 300; // King walking into attack
                }
                
                // Check if king is moving onto a file with enemy rooks/queens
                for (int r = 0; r < 8; ++r) {
                    int sq = make_sq(to_file, r);
                    int p = pos.board[sq];
                    if (p == EMPTY) continue;
                    bool enemy_piece = (mover == WHITE) ? is_black((Piece)p) : is_white((Piece)p);
                    if (enemy_piece) {
                        int pt3 = piece_type((Piece)p);
                        if (pt3 == WR || pt3 == WQ) {
                            score -= 200; // King walking onto file with enemy heavy piece
                        }
                    }
                }
            }
        }
        
        return score;
    }

    bool is_reverse_of_last(const Move &m) const {
        const Move &lm = (pos.side_to_move == WHITE) ? last_move_white : last_move_black;
        if (lm.from == -1) return false;
        if (m.captured != EMPTY || lm.captured != EMPTY) return false;
        return (m.from == lm.to && m.to == lm.from);
    }

    int reverse_move_penalty(const Move &m) const {
        int pen = 80;
        int pt = piece_type((Piece)m.moved);
        if (pt == WR) pen = 180;
        if (pt == WQ) pen = 140;
        return pen;
    }

    bool is_repeat_piece_move(const Move &m) const {
        const Move &lm = (pos.side_to_move == WHITE) ? last_move_white : last_move_black;
        if (lm.from == -1) return false;
        if (m.captured != EMPTY || lm.captured != EMPTY) return false;
        return (m.moved == lm.moved && m.from == lm.to);
    }

    int see_score(const Move &m) {
        // Very simple static exchange check: if destination is attacked and not defended, penalize.
        Side mover = (m.moved <= WK) ? WHITE : BLACK;
        Side opp = (mover == WHITE) ? BLACK : WHITE;
        int gain = (m.captured != EMPTY) ? piece_value(m.captured) : 0;
        int cost = piece_value(m.moved);
        bool attacked = is_square_attacked(m.to, opp);
        bool defended = is_square_attacked(m.to, mover);
        if (attacked && !defended) return gain - cost;
        if (attacked && defended) return gain - (cost / 2);
        return gain;
    }
    
    // === FORK DETECTION ===
    // Check if a knight on 'sq' attacks both king and queen/rook
    bool is_knight_fork_threat(int knight_sq, Side attacker) {
        int king_target = (attacker == WHITE) ? BK : WK;
        int queen_target = (attacker == WHITE) ? BQ : WQ;
        int rook_target = (attacker == WHITE) ? BR : WR;
        
        bool attacks_king = false;
        bool attacks_queen = false;
        bool attacks_rook = false;
        
        static const int knight_offsets[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
        int f = file_of(knight_sq);
        int r = rank_of(knight_sq);
        
        for (auto &o : knight_offsets) {
            int nf = f + o[0];
            int nr = r + o[1];
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
            int target_sq = make_sq(nf, nr);
            int p = pos.board[target_sq];
            if (p == king_target) attacks_king = true;
            if (p == queen_target) attacks_queen = true;
            if (p == rook_target) attacks_rook = true;
        }
        
        return attacks_king && (attacks_queen || attacks_rook);
    }
    
    // Find all knight fork threats for a side
    int count_knight_fork_threats(Side attacker) {
        int forks = 0;
        int knight_piece = (attacker == WHITE) ? WN : BN;
        
        for (int sq = 0; sq < 64; ++sq) {
            if (pos.board[sq] == knight_piece) {
                // Check all squares this knight can move to
                static const int knight_offsets[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
                int f = file_of(sq);
                int r = rank_of(sq);
                
                for (auto &o : knight_offsets) {
                    int nf = f + o[0];
                    int nr = r + o[1];
                    if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
                    int target_sq = make_sq(nf, nr);
                    int target = pos.board[target_sq];
                    // Can knight move here?
                    if (target == EMPTY || 
                        (attacker == WHITE && is_black((Piece)target)) ||
                        (attacker == BLACK && is_white((Piece)target))) {
                        // Simulate knight on this square and check for fork
                        int orig = pos.board[target_sq];
                        pos.board[sq] = EMPTY;
                        pos.board[target_sq] = knight_piece;
                        if (is_knight_fork_threat(target_sq, attacker)) {
                            forks++;
                        }
                        pos.board[target_sq] = orig;
                        pos.board[sq] = knight_piece;
                    }
                }
            }
        }
        return forks;
    }
    
    // Evaluate hanging pieces (pieces that are attacked but not defended)
    int evaluate_hanging_pieces() {
        int score = 0;
        
        for (int sq = 0; sq < 64; ++sq) {
            int p = pos.board[sq];
            if (p == EMPTY) continue;
            
            bool is_white_piece = is_white((Piece)p);
            Side owner = is_white_piece ? WHITE : BLACK;
            Side attacker = is_white_piece ? BLACK : WHITE;
            
            // Skip pawns and kings
            int pt = piece_type((Piece)p);
            if (pt == WP || pt == WK) continue;
            
            bool attacked = is_square_attacked(sq, attacker);
            bool defended = is_square_attacked(sq, owner);
            
            if (attacked && !defended) {
                // Hanging piece!
                int val = piece_value(p);
                if (is_white_piece) {
                    score -= val / 2; // White has hanging piece = bad for white
                } else {
                    score += val / 2; // Black has hanging piece = good for white
                }
            }
        }
        return score;
    }
    
    // Check if opponent has a winning tactical threat (fork, hanging piece capture)
    int evaluate_threats() {
        int score = 0;
        
        // Count fork threats
        int white_forks = count_knight_fork_threats(WHITE);
        int black_forks = count_knight_fork_threats(BLACK);
        
        // Fork threat is VERY dangerous
        score += white_forks * 300;
        score -= black_forks * 300;
        
        // Evaluate hanging pieces
        score += evaluate_hanging_pieces();
        
        return score;
    }

    void init_tables() {
        pst_pawn = {0, 0, 0, 0, 0, 0, 0, 0,
                    5, 5, 5, 5, 5, 5, 5, 5,
                    1, 1, 2, 3, 3, 2, 1, 1,
                    0, 0, 0, 2, 2, 0, 0, 0,
                    1, 1, 1, -2, -2, 1, 1, 1,
                    1, 1, 1, -3, -3, 1, 1, 1,
                    5, 5, 5, -5, -5, 5, 5, 5,
                    0, 0, 0, 0, 0, 0, 0, 0};

        pst_knight = {-5, -4, -3, -3, -3, -3, -4, -5,
                  -4, -2, 0, 0, 0, 0, -2, -4,
                  -3, 0, 1, 2, 2, 1, 0, -3,
                  -3, 1, 2, 2, 2, 2, 1, -3,
                  -3, 0, 2, 2, 2, 2, 0, -3,
                  -3, 1, 1, 2, 2, 1, 1, -3,
                  -4, -2, 0, 1, 1, 0, -2, -4,
                  -5, -4, -3, -3, -3, -3, -4, -5};

        pst_bishop = {-2, -1, -1, -1, -1, -1, -1, -2,
                  -1, 0, 0, 0, 0, 0, 0, -1,
                  -1, 0, 1, 1, 1, 1, 0, -1,
                  -1, 1, 1, 1, 1, 1, 1, -1,
                  -1, 0, 1, 1, 1, 1, 0, -1,
                  -1, 1, 1, 1, 1, 1, 1, -1,
                  -1, 1, 0, 0, 0, 0, 1, -1,
                  -2, -1, -1, -1, -1, -1, -1, -2};

        pst_rook = {0, 0, 0, 0, 0, 0, 0, 0,
                1, 2, 2, 2, 2, 2, 2, 1,
                -1, 0, 0, 0, 0, 0, 0, -1,
                -1, 0, 0, 0, 0, 0, 0, -1,
                -1, 0, 0, 0, 0, 0, 0, -1,
                -1, 0, 0, 0, 0, 0, 0, -1,
                -1, 0, 0, 0, 0, 0, 0, -1,
                0, 0, 0, 1, 1, 0, 0, 0};

        pst_queen = {-2, -1, -1, -1, -1, -1, -1, -2,
                 -1, 0, 0, 0, 0, 0, 0, -1,
                 -1, 0, 1, 1, 1, 1, 0, -1,
                 -1, 0, 1, 1, 1, 1, 0, -1,
                 0, 0, 1, 1, 1, 1, 0, -1,
                 -1, 1, 1, 1, 1, 1, 0, -1,
                 -1, 0, 1, 0, 0, 0, 0, -1,
                 -2, -1, -1, -1, -1, -1, -1, -2};

        pst_king = {-3, -4, -4, -5, -5, -4, -4, -3,
                    -3, -4, -4, -5, -5, -4, -4, -3,
                    -3, -4, -4, -5, -5, -4, -4, -3,
                    -3, -4, -4, -5, -5, -4, -4, -3,
                    -2, -3, -3, -4, -4, -3, -3, -2,
                    -1, -2, -2, -2, -2, -2, -2, -1,
                    2, 2, 0, 0, 0, 0, 2, 2,
                    2, 3, 1, 0, 0, 1, 3, 2};
    }

    char piece_to_char(Piece p) const {
        switch (p) {
            case WP: return 'P';
            case WN: return 'N';
            case WB: return 'B';
            case WR: return 'R';
            case WQ: return 'Q';
            case WK: return 'K';
            case BP: return 'p';
            case BN: return 'n';
            case BB: return 'b';
            case BR: return 'r';
            case BQ: return 'q';
            case BK: return 'k';
            default: return '.';
        }
    }

    int char_to_piece(char c) const {
        switch (c) {
            case 'P': return WP;
            case 'N': return WN;
            case 'B': return WB;
            case 'R': return WR;
            case 'Q': return WQ;
            case 'K': return WK;
            case 'p': return BP;
            case 'n': return BN;
            case 'b': return BB;
            case 'r': return BR;
            case 'q': return BQ;
            case 'k': return BK;
            default: return EMPTY;
        }
    }

    int promo_from_char(char c, Side side) const {
        switch (c) {
            case 'q': return side == WHITE ? WQ : BQ;
            case 'r': return side == WHITE ? WR : BR;
            case 'b': return side == WHITE ? WB : BB;
            case 'n': return side == WHITE ? WN : BN;
            default: return EMPTY;
        }
    }

    string move_to_uci(const Move &m) const {
        string s = sq_to_str(m.from) + sq_to_str(m.to);
        if (m.promotion != EMPTY) {
            char pc = tolower(piece_to_char((Piece)m.promotion));
            s.push_back(pc);
        }
        return s;
    }

    Side opposite(Side s) const { return s == WHITE ? BLACK : WHITE; }

    void update_hash() {
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

    bool is_square_attacked(int sq, Side by) const {
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

    bool in_check(Side side) {
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

    bool can_force_mate(int plies) {
        if (plies <= 0) return false;
        if (time_up()) return false;
        if (++mate_probe_nodes > MATE_PROBE_LIMIT) return false;

        auto moves = generate_legal_moves();
        if (moves.empty()) return false;

        for (auto &m : moves) {
            Undo u = make_move(m);
            bool mate_found = false;
            auto replies = generate_legal_moves();
            if (replies.empty()) {
                if (in_check(pos.side_to_move)) {
                    mate_found = true;
                }
            } else if (plies >= 2) {
                mate_found = true;
                for (auto &r : replies) {
                    Undo ur = make_move(r);
                    bool child = can_force_mate(plies - 2);
                    unmake_move(r, ur);
                    if (!child) {
                        mate_found = false;
                        break;
                    }
                }
            }
            unmake_move(m, u);
            if (mate_found) return true;
        }
        return false;
    }

    bool is_mate_trap(const Move &m, int plies) {
        mate_probe_nodes = 0;
        Undo u = make_move(m);
        bool trap = can_force_mate(plies);
        unmake_move(m, u);
        return trap;
    }

    bool has_mate_in_one() {
        auto moves = generate_legal_moves();
        for (auto &m : moves) {
            Undo u = make_move(m);
            // Quick check: must give check for mate
            if (!in_check(pos.side_to_move)) {
                unmake_move(m, u);
                continue;
            }
            auto replies = generate_legal_moves();
            bool mate = replies.empty();
            unmake_move(m, u);
            if (mate) return true;
        }
        return false;
    }
    
    // === FAST MATE SEARCH ===
    // Search for forced mate up to 'depth' plies (half-moves)
    // Returns: MATE_SCORE - depth if mate found, 0 if no mate
    // Node-limited to avoid timeout
    int mate_search_nodes = 0;
    static constexpr int MATE_SEARCH_NODE_LIMIT = 5000; // Fast cutoff for 500ms games
    
    int mate_search(int depth, bool maximizing) {
        mate_search_nodes++;
        if (mate_search_nodes > MATE_SEARCH_NODE_LIMIT) return 0; // Timeout
        if (time_up()) return 0; // Also check time!
        if (depth <= 0) return 0;
        
        auto moves = generate_legal_moves();
        
        // Check for checkmate or stalemate
        if (moves.empty()) {
            if (in_check(pos.side_to_move)) {
                // Checkmate!
                return maximizing ? -(MATE_SCORE - (10 - depth)) : (MATE_SCORE - (10 - depth));
            }
            return 0; // Stalemate
        }
        
        if (maximizing) {
            // Attacker's turn - looking for mate
            int best = 0;
            
            // Quick sort: only prioritize checks (fast check without make_move)
            // First pass: try all checks
            for (auto &m : moves) {
                Undo u = make_move(m);
                bool gives_check = in_check(pos.side_to_move);
                
                if (gives_check) {
                    int score = mate_search(depth - 1, false);
                    if (score > best) best = score;
                    if (best >= MATE_SCORE - 20) {
                        unmake_move(m, u);
                        return best; // Found mate!
                    }
                }
                unmake_move(m, u);
            }
            
            // Second pass: try captures only if depth is high enough
            if (depth >= 4 && best == 0) {
                for (auto &m : moves) {
                    if (m.captured != EMPTY) {
                        Undo u = make_move(m);
                        int score = mate_search(depth - 1, false);
                        if (score > best) best = score;
                        unmake_move(m, u);
                        if (best >= MATE_SCORE - 20) return best;
                    }
                }
            }
            
            return best;
        } else {
            // Defender's turn - must check ALL moves
            int worst = MATE_SCORE;
            
            for (auto &m : moves) {
                Undo u = make_move(m);
                int score = mate_search(depth - 1, true);
                unmake_move(m, u);
                
                if (score < worst) worst = score;
                if (worst == 0) return 0; // Found escape
            }
            
            return worst;
        }
    }
    
    // Check if we have mate in N moves (N = depth/2 moves each side)
    int has_mate_in_n(int depth) {
        mate_search_nodes = 0;
        return mate_search(depth * 2, true);
    }
    
    // Check if opponent has mate in N moves against us
    // AFTER we made our move, it's opponent's turn to find mate
    int opponent_has_mate_in_n(int depth) {
        mate_search_nodes = 0; // Reset node counter
        return mate_search(depth * 2, true);
    }
    
    // Quick check if current side can mate in 1 (very fast)
    bool opponent_has_mate_in_one() {
        return has_mate_in_one();
    }

    bool is_repetition() const {
        if (hash_history.empty()) return false;
        uint64_t h = pos.hash;
        int count = 0;
        for (auto v : hash_history) {
            if (v == h) count++;
            if (count >= 3) return true;
        }
        return false;
    }

    void generate_pseudo_moves(vector<Move> &moves) {
        moves.clear();
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
                            add_promo_moves(moves, sq, forward);
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
                                add_promo_moves(moves, sq, to, target);
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
                add_castling_moves(moves, sq, p);
            }
        }
    }

    void add_promo_moves(vector<Move> &moves, int from, int to, int captured = EMPTY) {
        int promos[4] = {WQ, WR, WB, WN};
        if (pos.side_to_move == BLACK) {
            promos[0] = BQ; promos[1] = BR; promos[2] = BB; promos[3] = BN;
        }
        for (int pr : promos) {
            Move m;
            m.from = from;
            m.to = to;
            m.promotion = pr;
            m.captured = captured;
            m.moved = pos.board[from];
            moves.push_back(m);
        }
    }

    void add_castling_moves(vector<Move> &moves, int king_sq, int king_piece) {
        if (king_piece == WK) {
            if ((pos.castling_rights & 1) && pos.board[make_sq(5,0)] == EMPTY && pos.board[make_sq(6,0)] == EMPTY) {
                if (!is_square_attacked(make_sq(4,0), BLACK) && !is_square_attacked(make_sq(5,0), BLACK) && !is_square_attacked(make_sq(6,0), BLACK)) {
                    Move m{king_sq, make_sq(6,0), EMPTY, false, true, EMPTY, king_piece};
                    moves.push_back(m);
                }
            }
            if ((pos.castling_rights & 2) && pos.board[make_sq(3,0)] == EMPTY && pos.board[make_sq(2,0)] == EMPTY && pos.board[make_sq(1,0)] == EMPTY) {
                if (!is_square_attacked(make_sq(4,0), BLACK) && !is_square_attacked(make_sq(3,0), BLACK) && !is_square_attacked(make_sq(2,0), BLACK)) {
                    Move m{king_sq, make_sq(2,0), EMPTY, false, true, EMPTY, king_piece};
                    moves.push_back(m);
                }
            }
        } else if (king_piece == BK) {
            if ((pos.castling_rights & 4) && pos.board[make_sq(5,7)] == EMPTY && pos.board[make_sq(6,7)] == EMPTY) {
                if (!is_square_attacked(make_sq(4,7), WHITE) && !is_square_attacked(make_sq(5,7), WHITE) && !is_square_attacked(make_sq(6,7), WHITE)) {
                    Move m{king_sq, make_sq(6,7), EMPTY, false, true, EMPTY, king_piece};
                    moves.push_back(m);
                }
            }
            if ((pos.castling_rights & 8) && pos.board[make_sq(3,7)] == EMPTY && pos.board[make_sq(2,7)] == EMPTY && pos.board[make_sq(1,7)] == EMPTY) {
                if (!is_square_attacked(make_sq(4,7), WHITE) && !is_square_attacked(make_sq(3,7), WHITE) && !is_square_attacked(make_sq(2,7), WHITE)) {
                    Move m{king_sq, make_sq(2,7), EMPTY, false, true, EMPTY, king_piece};
                    moves.push_back(m);
                }
            }
        }
    }

    bool is_opponent(int p, int target) const {
        if (target == EMPTY) return false;
        // Never allow capturing a king!
        if (target == WK || target == BK) return false;
        if (p <= WK) return target >= BP;
        return target <= WK;
    }

    Undo make_move(const Move &m) {
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

    void unmake_move(const Move &m, const Undo &u) {
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

    int evaluate() {
        int score = 0;
        for (int sq = 0; sq < 64; ++sq) {
            int p = pos.board[sq];
            if (p == EMPTY) continue;
            bool white_piece = p <= WK;
            int psq = white_piece ? sq : mirror_sq(sq);
            int val = 0;
            switch (piece_type((Piece)p)) {
                case WP: {
                    val = 100 + pst_pawn[psq];
                    // BONUS: Pawn about to promote (7th/2nd rank)
                    int rank = rank_of(sq);
                    if (white_piece && rank == 6) val += 500; // White pawn on 7th rank
                    if (!white_piece && rank == 1) val += 500; // Black pawn on 2nd rank
                    break;
                }
                case WN: val = 320 + pst_knight[psq]; break;
                case WB: val = 330 + pst_bishop[psq]; break;
                case WR: val = 500 + pst_rook[psq]; break;
                case WQ: val = 900 + pst_queen[psq]; break;
                case WK: val = 20000 + pst_king[psq]; break;
                default: break;
            }
            if (p >= BP) val = -val;
            score += val;
        }

        int dev = 0;
        if (pos.fullmove_number <= 12) {
            // Reward knight/bishop development (leaving home squares)
            if (pos.board[make_sq(1,0)] != WN) dev += 15;  // Nb1 moved
            if (pos.board[make_sq(6,0)] != WN) dev += 15;  // Ng1 moved
            if (pos.board[make_sq(2,0)] != WB) dev += 12;  // Bc1 moved
            if (pos.board[make_sq(5,0)] != WB) dev += 12;  // Bf1 moved
            if (pos.board[make_sq(1,7)] != BN) dev -= 15;  // Nb8 moved
            if (pos.board[make_sq(6,7)] != BN) dev -= 15;  // Ng8 moved
            if (pos.board[make_sq(2,7)] != BB) dev -= 12;  // Bc8 moved
            if (pos.board[make_sq(5,7)] != BB) dev -= 12;  // Bf8 moved

            // Penalize early queen moves (queen not on d1/d8)
            if (pos.board[make_sq(3,0)] != WQ && pos.fullmove_number <= 8) dev -= 25;
            if (pos.board[make_sq(3,7)] != BQ && pos.fullmove_number <= 8) dev += 25;

            // HEAVILY penalize rooks on b1/g1/b8/g8 (useless positions)
            // These are squares where rooks go during Ra1-Rb1 shuffle
            if (pos.board[make_sq(1,0)] == WR) dev -= 60;  // Rook on b1 = BAD
            if (pos.board[make_sq(6,0)] == WR) dev -= 60;  // Rook on g1 = BAD (unless castled)
            if (pos.board[make_sq(1,7)] == BR) dev += 60;  // Rook on b8 = BAD
            if (pos.board[make_sq(6,7)] == BR) dev += 60;  // Rook on g8 = BAD

            // Also penalize rooks moving to c1/f1 early without purpose
            if (pos.board[make_sq(2,0)] == WR && pos.fullmove_number <= 6) dev -= 30;
            if (pos.board[make_sq(5,0)] == WR && pos.fullmove_number <= 6) dev -= 30;
            if (pos.board[make_sq(2,7)] == BR && pos.fullmove_number <= 6) dev += 30;
            if (pos.board[make_sq(5,7)] == BR && pos.fullmove_number <= 6) dev += 30;
        }

        // Castling bonus
        if (pos.fullmove_number <= 16) {
            if (pos.board[make_sq(6,0)] == WK || pos.board[make_sq(2,0)] == WK) dev += 30;
            if (pos.board[make_sq(6,7)] == BK || pos.board[make_sq(2,7)] == BK) dev -= 30;
        }

        score += dev;
        
        // === KING SAFETY ===
        int king_safety = evaluate_king_safety();
        score += king_safety;
        
        // === DEVELOPMENT QUALITY ===
        int development = evaluate_development_quality();
        score += development;
        
        // === TACTICAL THREATS (forks, hanging pieces) ===
        int threats = evaluate_threats();
        score += threats;
        
        return (pos.side_to_move == WHITE) ? score : -score;
    }
    
    // Evaluate king safety - especially f7/f2 weakness
    int evaluate_king_safety() {
        int safety = 0;
        
        // Find kings
        int wk_sq = -1, bk_sq = -1;
        for (int sq = 0; sq < 64; ++sq) {
            if (pos.board[sq] == WK) wk_sq = sq;
            if (pos.board[sq] == BK) bk_sq = sq;
        }
        
        // === CRITICAL: King in the center of the board is DEADLY ===
        // King should be on rank 0/1 for white or rank 6/7 for black
        // King in the middle (ranks 2-5) is extremely dangerous
        
        if (wk_sq != -1) {
            int wk_rank = rank_of(wk_sq);
            int wk_file = file_of(wk_sq);
            
            // White king on ranks 2-5 (middle of board) = disaster
            if (wk_rank >= 2 && wk_rank <= 5) {
                safety -= 300; // Huge penalty
                // Even worse if in center files
                if (wk_file >= 2 && wk_file <= 5) {
                    safety -= 200; // King in center = death
                }
                // Check if king is attacked
                if (is_square_attacked(wk_sq, BLACK)) {
                    safety -= 150;
                }
            }
            // King on rank 1 but moved from e1 (not castled) - minor penalty
            else if (wk_rank == 0 && wk_file != 6 && wk_file != 2 && wk_file != 4) {
                safety -= 50; // King walked but didn't castle
            }
        }
        
        if (bk_sq != -1) {
            int bk_rank = rank_of(bk_sq);
            int bk_file = file_of(bk_sq);
            
            // Black king on ranks 2-5 (middle of board) = disaster  
            if (bk_rank >= 2 && bk_rank <= 5) {
                safety += 300; // Huge bonus for white (black king exposed)
                // Even worse if in center files
                if (bk_file >= 2 && bk_file <= 5) {
                    safety += 200;
                }
                // Check if king is attacked
                if (is_square_attacked(bk_sq, WHITE)) {
                    safety += 150;
                }
            }
            // King on rank 7 but moved from e8 (not castled) - minor penalty
            else if (bk_rank == 7 && bk_file != 6 && bk_file != 2 && bk_file != 4) {
                safety += 50;
            }
        }
        
        // === F7 weakness for Black ===
        int f7 = make_sq(5, 6); // f7
        if (pos.board[f7] == BP) {
            // f7 pawn is there, check if it's defended properly
            // If king hasn't castled and f7 is under attack, big penalty
            if (bk_sq == make_sq(4, 7)) { // King on e8 (hasn't castled)
                if (is_square_attacked(f7, WHITE)) {
                    safety += 80; // Bonus for white (black is weak)
                }
                // If knight on e7 blocks the king, penalty
                if (pos.board[make_sq(4, 6)] == BN) {
                    safety += 25; // Knight on e7 is awkward
                }
            }
        } else if (pos.board[f7] == EMPTY) {
            // f7 is open - very dangerous if king on e8
            if (bk_sq == make_sq(4, 7)) {
                safety += 60;
            }
        }
        
        // === F2 weakness for White ===
        int f2 = make_sq(5, 1); // f2
        if (pos.board[f2] == WP) {
            if (wk_sq == make_sq(4, 0)) { // King on e1
                if (is_square_attacked(f2, BLACK)) {
                    safety -= 80;
                }
            }
        } else if (pos.board[f2] == EMPTY) {
            if (wk_sq == make_sq(4, 0)) {
                safety -= 60;
            }
        }
        
        // === Penalty for king in center after move 8 ===
        if (pos.fullmove_number > 8) {
            if (wk_sq == make_sq(4, 0)) safety -= 40; // White king still on e1
            if (bk_sq == make_sq(4, 7)) safety += 40; // Black king still on e8
        }
        
        // === Check if queen can attack f7/f2 with support ===
        // Detect Qxf7# patterns
        int e5 = make_sq(4, 4);
        if (pos.board[e5] == WP || is_square_attacked(e5, WHITE)) {
            // White controls e5, diagonal to f7 is open
            if (bk_sq == make_sq(4, 7) && pos.fullmove_number <= 15) {
                safety += 30; // Black should be careful
            }
        }
        
        return safety;
    }
    
    // Evaluate development quality - penalize bad piece placement
    int evaluate_development_quality() {
        int dev = 0;
        
        // === Penalize bishop on edge files in opening ===
        if (pos.fullmove_number <= 12) {
            for (int sq = 0; sq < 64; ++sq) {
                int p = pos.board[sq];
                int f = file_of(sq);
                int r = rank_of(sq);
                
                // Bishop on a or h file = usually bad
                if ((p == WB || p == BB) && (f == 0 || f == 7)) {
                    if (p == WB) dev -= 35;
                    else dev += 35;
                }
                
                // Bishop on a2/b1 or a7/b8 = grabbing pawn, losing tempo
                if (p == WB && (sq == make_sq(0, 1) || sq == make_sq(1, 0))) {
                    dev -= 50; // Bad bishop position
                }
                if (p == BB && (sq == make_sq(0, 6) || sq == make_sq(1, 7))) {
                    dev += 50;
                }
                // Black bishop on a2 = terrible, wasted tempo
                if (p == BB && sq == make_sq(0, 1)) {
                    dev += 80; // Bonus for white - black wasted tempo
                }
                if (p == WB && sq == make_sq(0, 6)) {
                    dev -= 80;
                }
                
                // Penalize pieces blocking central pawns
                if (p == BN || p == BB) {
                    // Knight or bishop on e7/d7 blocking pawns
                    if (sq == make_sq(4, 6) && pos.board[make_sq(4, 4)] != BP) {
                        dev += 20; // Piece on e7 blocking e-pawn
                    }
                    if (sq == make_sq(3, 6) && pos.board[make_sq(3, 4)] != BP) {
                        dev += 20; // Piece on d7 blocking d-pawn
                    }
                }
                if (p == WN || p == WB) {
                    if (sq == make_sq(4, 1) && pos.board[make_sq(4, 3)] != WP) {
                        dev -= 20;
                    }
                    if (sq == make_sq(3, 1) && pos.board[make_sq(3, 3)] != WP) {
                        dev -= 20;
                    }
                }
            }
            
            // === Penalize moving same piece twice in opening ===
            // (Already handled in move_score, but reinforce here)
            
            // === Bonus for controlling center (d4, d5, e4, e5) ===
            int center_control = 0;
            int center_sqs[] = {make_sq(3,3), make_sq(3,4), make_sq(4,3), make_sq(4,4)};
            for (int csq : center_sqs) {
                if (is_square_attacked(csq, WHITE)) center_control += 8;
                if (is_square_attacked(csq, BLACK)) center_control -= 8;
                int p = pos.board[csq];
                if (p == WP || p == WN) center_control += 15;
                if (p == BP || p == BN) center_control -= 15;
            }
            dev += center_control;
        }
        
        return dev;
    }

    int mirror_sq(int sq) const {
        int f = file_of(sq);
        int r = rank_of(sq);
        return make_sq(f, 7 - r);
    }

    int node_count = 0;
    
    int alpha_beta(int depth, int alpha, int beta, Move &best) {
        node_count++;
        if (time_up()) return 0;
        if (depth == 0) return quiescence(alpha, beta);

        if (pos.halfmove_clock >= 100 || is_repetition()) {
            int draw_score = evaluate() / 10;
            draw_score = clamp(draw_score, -20, 20);
            return draw_score;
        }

        bool in_check_now = in_check(pos.side_to_move);
        
        // === CHECK EXTENSION: Extend search when in check ===
        // Only extend once per check to avoid explosion
        // This is done in the recursive call instead

        auto moves = generate_legal_moves();
        if (moves.empty()) {
            if (in_check_now) return -MATE_SCORE + (100 - depth);
            return 0;
        }

        sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b) {
            return move_score(a) > move_score(b);
        });

        int best_score = -INF;
        
        for (auto &m : moves) {
            // Basic blunder filter for queen moves
            if (piece_type((Piece)m.moved) == WQ && see_score(m) < -200) {
                continue;
            }
            // Check BEFORE make_move while position state is correct!
            bool is_reverse = is_reverse_of_last(m);
            bool is_repeat = is_repeat_piece_move(m);
            
            Undo u = make_move(m);
            
            // === CRITICAL: Check if this move allows mate in 1 ===
            if (has_mate_in_one()) {
                unmake_move(m, u);
                continue; // Skip this move - it allows opponent to mate us
            }
            
            // === CHECK EXTENSION: If this move gives check, extend search ===
            bool gives_check = in_check(pos.side_to_move);
            int extension = gives_check ? 1 : 0;
            
            Move child_best;
            int score = -alpha_beta(depth - 1 + extension, -beta, -alpha, child_best);
            
            unmake_move(m, u);
            
            // Apply penalties AFTER unmake but using pre-computed flags
            if (is_reverse) {
                score -= reverse_move_penalty(m);
            }
            if (is_repeat) {
                score -= 120;
            }
            
            // Penalize pointless rook moves in opening
            int pt = piece_type((Piece)m.moved);
            if (pt == WR && m.captured == EMPTY && pos.fullmove_number <= 15) {
                int to_file = file_of(m.to);
                int to_rank = rank_of(m.to);
                if (to_rank == 0 || to_rank == 7) {
                    if (to_file == 1 || to_file == 2 || to_file == 5 || to_file == 6) {
                        score -= 200;
                    }
                    if (pos.fullmove_number <= 10) {
                        score -= 150;
                    }
                }
            }
            
            // === CRITICAL: Penalize king moves to center ===
            if (pt == WK) {
                int to_rank = rank_of(m.to);
                int to_file = file_of(m.to);
                if (to_rank >= 2 && to_rank <= 5) {
                    score -= 500;
                    if (to_file >= 2 && to_file <= 5) {
                        score -= 300;
                    }
                }
            }
            
            // === CRITICAL: Penalize trading queen for minor piece ===
            if (pt == WQ && m.captured != EMPTY) {
                int cap_pt = piece_type((Piece)m.captured);
                if (cap_pt == WB || cap_pt == WN || cap_pt == WP) {
                    Side mover = (m.moved <= WK) ? WHITE : BLACK;
                    Side opp = (mover == WHITE) ? BLACK : WHITE;
                    if (is_square_attacked(m.to, opp)) {
                        score -= 600;
                    }
                }
            }
            
            if (stop_search) return 0;
            if (score > best_score) {
                best_score = score;
                best = m;
            }
            if (score > alpha) alpha = score;
            if (alpha >= beta) break;
        }
        
        return best_score;
    }

    int quiescence(int alpha, int beta) {
        if (time_up()) return 0;
        int stand = evaluate();
        if (stand >= beta) return beta;
        if (stand > alpha) alpha = stand;
        
        // Delta pruning: if we're very far behind, don't search
        int delta = 900; // queen value
        if (stand + delta < alpha) return alpha;

        vector<Move> moves;
        generate_pseudo_moves(moves);
        
        // Sort captures by MVV-LVA
        vector<Move> captures;
        for (auto &m : moves) {
            // Include captures, en passant, AND promotions (very important!)
            if (m.captured != EMPTY || m.is_en_passant || m.promotion != EMPTY) {
                captures.push_back(m);
            }
        }
        
        // Simple MVV-LVA sort
        sort(captures.begin(), captures.end(), [&](const Move &a, const Move &b) {
            return piece_value(a.captured) - piece_value(a.moved)/10 > 
                   piece_value(b.captured) - piece_value(b.moved)/10;
        });
        
        for (auto &m : captures) {
            Undo u = make_move(m);
            if (in_check(opposite(pos.side_to_move))) {
                unmake_move(m, u);
                continue;
            }
            int score = -quiescence(-beta, -alpha);
            unmake_move(m, u);
            if (stop_search) return 0;
            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }

    bool time_up() {
        if (stop_search) {
            return true;
        }
        auto now = chrono::steady_clock::now();
        int elapsed = (int)chrono::duration_cast<chrono::milliseconds>(now - search_start).count();
        if (elapsed >= time_limit) {
            stop_search = true;
            return true;
        }
        return false;
    }
};

} // namespace chess

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    chess::Engine engine;

    string line;
    while (getline(cin, line)) {
        if (line == "uci") {
            cout << "id name BlitzEngine" << std::endl;
            cout << "id author GitHub Copilot" << std::endl;
            cout << "uciok" << std::endl;
        } else if (line == "isready") {
            cout << "readyok" << std::endl;
        } else if (line == "ucinewgame") {
            engine.new_game();
        } else if (line.rfind("position", 0) == 0) {
            engine.set_position_from_uci(line);
        } else if (line.rfind("go", 0) == 0) {
            int depth = 10;
            int movetime = -1;
            int wtime = 300000, btime = 300000, winc = 0, binc = 0;
            stringstream ss(line);
            string token;
            ss >> token; // go
            while (ss >> token) {
                if (token == "depth") ss >> depth;
                else if (token == "movetime") ss >> movetime;
                else if (token == "wtime") ss >> wtime;
                else if (token == "btime") ss >> btime;
                else if (token == "winc") ss >> winc;
                else if (token == "binc") ss >> binc;
            }
            int time_ms = movetime > 0 ? movetime : engine.compute_time_ms(wtime, btime, winc, binc);
            chess::Move best = engine.search_bestmove(depth, time_ms);
            if (best.from == -1) {
                cout << "bestmove 0000" << std::endl;
            } else {
                cout << "bestmove " << engine.move_to_uci_public(best) << std::endl;
            }
        } else if (line == "legal") {
            cout << engine.legal_moves_uci() << std::endl;
        } else if (line == "quit") {
            break;
        }
    }
    return 0;
}
