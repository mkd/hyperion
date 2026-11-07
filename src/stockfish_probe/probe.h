#ifndef STOCKFISH_PROBE_H
#define STOCKFISH_PROBE_H

namespace Stockfish {
namespace Probe {

/// Loads the NNUE network from the given file path.
void init(const char *netFile = nullptr);

/// Evaluates the position given as a FEN string (side-to-move relative, centipawns).
int eval(const char *fen);

} // namespace Probe
} // namespace Stockfish

#endif // STOCKFISH_PROBE_H
