#include "engine.h"

#include <iostream>
#include <sstream>
#include <string>

using namespace std;
using namespace chess;

int main() {
    Engine engine;
    string line;

    while (getline(cin, line)) {
        if (line == "uci") {
            cout << "id name Blitz Engine" << endl;
            cout << "id author Chess Dev" << endl;
            cout << "uciok" << endl;
        } else if (line == "isready") {
            cout << "readyok" << endl;
        } else if (line == "ucinewgame") {
            engine.new_game();
        } else if (line.substr(0, 8) == "position") {
            engine.set_position_from_uci(line);
        } else if (line.substr(0, 2) == "go") {
            int max_depth = 64;
            int time_ms = 5000;

            istringstream iss(line);
            string token;
            int wtime = 0, btime = 0, winc = 0, binc = 0;
            bool has_time = false;
            int movetime = 0;

            iss >> token; // skip "go"
            while (iss >> token) {
                if (token == "depth") { iss >> max_depth; }
                else if (token == "wtime") { iss >> wtime; has_time = true; }
                else if (token == "btime") { iss >> btime; has_time = true; }
                else if (token == "winc") { iss >> winc; }
                else if (token == "binc") { iss >> binc; }
                else if (token == "movetime") { iss >> movetime; }
            }

            if (movetime > 0) {
                time_ms = movetime;
            } else if (has_time) {
                time_ms = engine.compute_time_ms(wtime, btime, winc, binc);
            }

            Move best = engine.search_bestmove(max_depth, time_ms);
            cout << "bestmove " << move_to_uci(best) << endl;
        } else if (line == "quit") {
            break;
        } else if (line == "d") {
            // Debug: print FEN
            cout << engine.board.to_fen() << endl;
        }
    }
    return 0;
}
