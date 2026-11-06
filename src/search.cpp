/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>

  Hyperion is a successor of Gargantua, which is derived from Chess0 and
  inspired by Code Monkey King's bbc-1.4.

  This file is part of Hyperion.

  Hyperion is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Hyperion is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/**
 * @file search.cpp
 * @brief The search: principal-variation alpha-beta (`negamax`), quiescence
 *        search, static exchange evaluation, move ordering, history updates,
 *        iterative deepening with aspiration windows, and time management.
 *
 * All functions here run on the thread selected by `current_thread`; the board
 * and heuristic tables they touch are that thread's private copy.
 */

#include "search.h"
#include "bitboard.h"
#include "eval.h"
#include "movgen.h"
#include "position.h"
#include "tbprobe.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

Limits_t Limits;

int MultiPV = DefaultMultiPV;

namespace {

inline void print_score(std::ostream &os, int score) {
    if (score > MateScore && score <= MateValue) {
        os << "score mate " << (MateValue - score) / 2 + 1 << " ";
    } else if (score >= -MateValue && score < -MateScore) {
        os << "score mate " << -(score + MateValue) / 2 - 1 << " ";
    } else if (score > TBWinScore - MaxPly && score <= TBWinScore) {
        const int dist = std::max(1, (TBWinScore - score + 1) / 2);
        os << "score mate " << dist << " ";
    } else if (score >= -TBWinScore && score < -TBWinScore + MaxPly) {
        const int dist = std::max(1, (score - (-TBWinScore) + 1) / 2);
        os << "score mate -" << dist << " ";
    } else {
        os << "score cp " << score << " ";
    }
}

inline uint64_t to_fathom_bb(uint64_t bb) noexcept {
    return __builtin_bswap64(bb);
}

inline unsigned to_fathom_sq(int sq) noexcept {
    return (sq == NoSq) ? 0 : static_cast<unsigned>(sq ^ 56);
}

inline int from_fathom_sq(int sq) noexcept {
    return sq ^ 56;
}

} // namespace
static int multi_pv_moves[256];
static int multi_pv_count = 0;

/// Late-move-reduction table, indexed by [depth][move number] (filled by initLMR()).
static int LMRTable[MaxSearchDepth][64];

/// Piece values used by static exchange evaluation (index 12 = empty square).
static constexpr int see_piece_values[13] = {
    100, 300, 310, 500, 950, 20000,
    100, 300, 310, 500, 950, 20000,
    0
};

/// Victim values (indexed by piece type) used for MVV-LVA capture ordering.
static constexpr int mvv_victim_score[6] = {
    100, 300, 310, 500, 950, 10000
};

void initLMR() noexcept {
    for (int depth = 1; depth < MaxSearchDepth; ++depth) {
        for (int moves = 1; moves < 64; ++moves) {
            LMRTable[depth][moves] = static_cast<int>(0.75 + std::log(depth) * std::log(moves) / 2.25);
        }
    }
}

/// Raises the global stop flag once the hard time limit has been reached.
static void checkTime() noexcept {
    if (Limits.ponder.load(std::memory_order_relaxed))
        return;
    if (Limits.timeset && getTimeMs() >= Limits.max_stoptime) {
        Limits.stopped.store(true, std::memory_order_relaxed);
    }
}

/// Stockfish-style "gravity" update: moves `entry` towards `bonus` while keeping it bounded by MaxHistory.
static void updateHistoryStat(int &entry, int bonus) noexcept {
    const int clamped = std::clamp(bonus, -MaxHistory, MaxHistory);
    entry += clamped - entry * std::abs(clamped) / MaxHistory;
}

/// Applies `bonus` to butterfly history, multi-ply continuation histories (1, 2, 4 plies), and pawn history.
static void updateQuietHistory(int move, int bonus) noexcept {
    const int from  = getMoveSource(move);
    const int to    = getMoveTarget(move);
    const int piece = getMovePiece(move);

    updateHistoryStat(history[sideToMove][from][to], bonus);

    const int pkey = static_cast<int>((pawn_key ^ (pawn_key >> 32)) & PawnHistMask);
    updateHistoryStat(pawn_history[pkey][piece][to], bonus);

    if (ply >= 1 && current_move[ply - 1]) {
        const int prev1 = current_move[ply - 1];
        updateHistoryStat(cont_history[0][getMovePiece(prev1)][getMoveTarget(prev1)][piece][to], bonus);
    }
    if (ply >= 2 && current_move[ply - 2]) {
        const int prev2 = current_move[ply - 2];
        updateHistoryStat(cont_history[1][getMovePiece(prev2)][getMoveTarget(prev2)][piece][to], bonus);
    }
    if (ply >= 4 && current_move[ply - 4]) {
        const int prev4 = current_move[ply - 4];
        updateHistoryStat(cont_history[2][getMovePiece(prev4)][getMoveTarget(prev4)][piece][to], bonus);
    }
}

/// @return the type of the piece captured by `move` (a pawn for en passant or non-captures).
static int getCapturedPieceType(int move) noexcept {
    if (getEp(move)) return P;
    const int cap = mailbox[getMoveTarget(move)];
    return (cap != NoPiece) ? pieceType(cap) : P;
}

/// Applies `bonus` to the capture history entry and 1-ply capture continuation history of `move`.
static void updateCaptureHistory(int move, int bonus) noexcept {
    const int piece   = getMovePiece(move);
    const int to      = getMoveTarget(move);
    const int capType = getCapturedPieceType(move);
    updateHistoryStat(capture_history[piece][to][capType], bonus);

    if (ply >= 1 && current_move[ply - 1]) {
        const int prev1 = current_move[ply - 1];
        updateHistoryStat(capture_cont_history[getMovePiece(prev1)][getMoveTarget(prev1)][piece][to], bonus);
    }
}

/**
 * Finds the least valuable piece of colour `side` attacking `sq` given the
 * current occupancy.
 * @param[out] attackerPiece piece code of the attacker found
 * @return single-bit bitboard of the attacker, or 0 if there is none
 */
static Bitboard getSmallestAttacker(int sq, int side, int &attackerPiece) noexcept {
    const int off = (side == White) ? 0 : 6; // piece-code offset of `side`
    const Bitboard occ = occupancies[Both];

    if (const Bitboard bb = PawnAttacks[side ^ 1][sq] & bitboards[P + off]) {
        attackerPiece = P + off;
        return SqBB[ls1b(bb)];
    }
    if (const Bitboard bb = KnightAttacks[sq] & bitboards[N + off]) {
        attackerPiece = N + off;
        return SqBB[ls1b(bb)];
    }
    if (const Bitboard bb = getBishopAttacks(sq, occ) & bitboards[B + off]) {
        attackerPiece = B + off;
        return SqBB[ls1b(bb)];
    }
    if (const Bitboard bb = getRookAttacks(sq, occ) & bitboards[R + off]) {
        attackerPiece = R + off;
        return SqBB[ls1b(bb)];
    }
    if (const Bitboard bb = (getBishopAttacks(sq, occ) | getRookAttacks(sq, occ)) & bitboards[Q + off]) {
        attackerPiece = Q + off;
        return SqBB[ls1b(bb)];
    }
    if (const Bitboard bb = KingAttacks[sq] & bitboards[K + off]) {
        attackerPiece = K + off;
        return SqBB[ls1b(bb)];
    }
    return 0ULL;
}

/**
 * Static exchange evaluation: the material outcome (centipawns) of the capture
 * sequence started by `move` on its target square, assuming both sides always
 * recapture with their least valuable attacker and may stop at any time.
 */
int see(int move) noexcept {
    if (getCastle(move)) return 0;

    const int fromSq = getMoveSource(move);
    const int toSq   = getMoveTarget(move);
    const int piece  = getMovePiece(move);
    const int promo  = getPromo(move);
    const int ep     = getEp(move);

    int gain[32];
    int d = 0;

    int targetPiece = NoPiece;
    if (ep) {
        targetPiece = (sideToMove == White) ? p : P;
        gain[d] = see_piece_values[P];
    } else if (getMoveCapture(move)) {
        targetPiece = mailbox[toSq];
        gain[d] = (targetPiece != NoPiece) ? see_piece_values[targetPiece] : 0;
    } else {
        gain[d] = 0;
    }

    if (promo) {
        gain[d] += see_piece_values[promo] - see_piece_values[P];
    }

    Bitboard bitboards_bak[12];
    Bitboard occupancies_bak[3];
    std::memcpy(bitboards_bak, bitboards, sizeof(bitboards));
    std::memcpy(occupancies_bak, occupancies, sizeof(occupancies));

    popBit(bitboards[piece], fromSq);
    popBit(occupancies[sideToMove], fromSq);

    if (targetPiece != NoPiece) {
        const int capSq = ep ? ((sideToMove == White) ? (toSq + 8) : (toSq - 8)) : toSq;
        popBit(bitboards[targetPiece], capSq);
        popBit(occupancies[sideToMove ^ 1], capSq);
    }

    occupancies[Both] = occupancies[White] | occupancies[Black];

    int attackerPiece = promo ? promo : piece;
    int side = sideToMove ^ 1;

    while (true) {
        ++d;
        gain[d] = see_piece_values[attackerPiece] - gain[d - 1];

        if (std::max(-gain[d - 1], gain[d]) < 0)
            break;

        const Bitboard attackerBB = getSmallestAttacker(toSq, side, attackerPiece);
        if (!attackerBB)
            break;

        const int attackerSq = ls1b(attackerBB);
        popBit(bitboards[attackerPiece], attackerSq);
        popBit(occupancies[side], attackerSq);
        occupancies[Both] = occupancies[White] | occupancies[Black];

        side ^= 1;
        if (d >= 30) break;
    }

    while (--d) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    }

    std::memcpy(bitboards, bitboards_bak, sizeof(bitboards));
    std::memcpy(occupancies, occupancies_bak, sizeof(occupancies));

    return gain[0];
}

/**
 * @return true when see(move) >= threshold.  Cheap upper/lower bounds are
 *         checked first so the full exchange is only simulated when needed.
 */
bool see_ge(int move, int threshold) noexcept {
    if (getCastle(move)) return 0 >= threshold;

    const int toSq  = getMoveTarget(move);
    const int piece = getMovePiece(move);
    const int promo = getPromo(move);
    const int ep    = getEp(move);

    int initial_gain = 0;
    if (ep) {
        initial_gain = see_piece_values[P];
    } else if (getMoveCapture(move)) {
        const int cap = mailbox[toSq];
        if (cap != NoPiece)
            initial_gain = see_piece_values[cap];
    }
    if (promo) {
        initial_gain += see_piece_values[promo] - see_piece_values[P];
    }

    // Fast upper-bound check: even if opponent never recaptures, can we reach threshold?
    if (initial_gain < threshold)
        return false;

    // Fast lower-bound check: even if opponent captures our moving piece for free, do we still reach threshold?
    const int moved_val = see_piece_values[promo ? promo : piece];
    if (initial_gain - moved_val >= threshold)
        return true;

    return see(move) >= threshold;
}

/**
 * Assigns an ordering score to `move`: PV move, TT move, good captures
 * (MVV-LVA + capture history, SEE-filtered), killers, countermove, quiet
 * history, and finally losing captures.
 */
int scoreMove(int move, int tt_move) noexcept {
    if (scorePV && move == pv_table[0][ply])
        return 3500000;

    if (move == tt_move)
        return 3000000;

    const int fromSq  = getMoveSource(move);
    const int toSq    = getMoveTarget(move);
    const int piece   = getMovePiece(move);
    const int promo   = getPromo(move);
    const int capture = getMoveCapture(move);

    if (capture || promo) {
        int victim_score = 0;
        int cap_type = P;
        if (capture) {
            if (getEp(move)) {
                victim_score = mvv_victim_score[P];
                cap_type = P;
            } else {
                const int cap = mailbox[toSq];
                if (cap != NoPiece) {
                    cap_type = pieceType(cap);
                    victim_score = mvv_victim_score[cap_type];
                }
            }
        }
        if (promo) {
            victim_score += mvv_victim_score[pieceType(promo)];
        }

        int cap_hist = capture_history[piece][toSq][cap_type];
        if (ply >= 1 && current_move[ply - 1]) {
            const int prev1 = current_move[ply - 1];
            cap_hist += capture_cont_history[getMovePiece(prev1)][getMoveTarget(prev1)][piece][toSq];
        }
        const int mvv_lva = victim_score * 16 - pieceType(piece) + cap_hist / 16;

        // Underpromotions (except knight) go below good captures
        if (promo && promo != Q && promo != q && promo != N && promo != n) {
            return -600000 + mvv_lva;
        }

        if (see_ge(move, -cap_hist / 32)) {
            return 1000000 + mvv_lva;
        } else {
            return -500000 + mvv_lva;
        }
    }

    if (move == killers[0][ply])
        return 900000;
    if (move == killers[1][ply])
        return 850000;

    if (ply >= 1 && current_move[ply - 1]) {
        const int prev = current_move[ply - 1];
        if (move == countermoves[getMovePiece(prev)][getMoveTarget(prev)])
            return 800000;
    }

    int h = history[sideToMove][fromSq][toSq];
    const int pkey = static_cast<int>((pawn_key ^ (pawn_key >> 32)) & PawnHistMask);
    h += pawn_history[pkey][piece][toSq];

    if (ply >= 1 && current_move[ply - 1]) {
        const int prev1 = current_move[ply - 1];
        h += cont_history[0][getMovePiece(prev1)][getMoveTarget(prev1)][piece][toSq];
    }
    if (ply >= 2 && current_move[ply - 2]) {
        const int prev2 = current_move[ply - 2];
        h += cont_history[1][getMovePiece(prev2)][getMoveTarget(prev2)][piece][toSq];
    }
    if (ply >= 4 && current_move[ply - 4]) {
        const int prev4 = current_move[ply - 4];
        h += cont_history[2][getMovePiece(prev4)][getMoveTarget(prev4)][piece][toSq];
    }

    return std::clamp(h, -400000, 750000);
}

/// Selection-sort step: swaps the best-scored remaining move into `startIdx` and returns it.
static int pickNextMove(MoveList_t &moveList, int *scores, int startIdx) noexcept {
    int bestIdx = startIdx;
    int bestScore = scores[startIdx];
    for (int i = startIdx + 1; i < moveList.count; ++i) {
        if (scores[i] > bestScore) {
            bestScore = scores[i];
            bestIdx = i;
        }
    }
    if (bestIdx != startIdx) {
        std::swap(moveList.moves[startIdx], moveList.moves[bestIdx]);
        std::swap(scores[startIdx], scores[bestIdx]);
    }
    return moveList.moves[startIdx];
}

/// While following the previous iteration's PV, enables PV-move scoring if the PV move is in the list.
static void enablePVScoring(MoveList_t &moveList) noexcept {
    followPV = false;
    for (int i = 0; i < moveList.count; ++i) {
        if (pv_table[0][ply] == moveList.moves[i]) {
            scorePV = true;
            followPV = true;
            break;
        }
    }
}

/**
 * Quiescence search: resolves captures and promotions (all evasions when in
 * check) until the position is quiet, so the static evaluation is reliable.
 */
int qsearch(int alpha, int beta) {
    if ((++nodes & 1023) == 0)
        checkTime();

    if (Limits.stopped.load(std::memory_order_relaxed))
        return 0;

    if (ply > seldepth)
        seldepth = ply;

    if (ply < MaxPly)
        pv_length[ply] = ply;

    if (ply && isDraw())
        return 0;

    if (ply >= MaxPly - 4 || repetition_index >= 1020)
        return adjustEvalWithCorrHist(evaluate());

    const bool pv_node = (beta - alpha) > 1;
    const TTProbeResult tt_res = TT::probe_ex(alpha, beta, 0, pv_node);
    if (!pv_node && tt_res.cutoff)
        return tt_res.score;

    const bool inCheck = isSquareAttacked(
        ls1b(sideToMove == White ? bitboards[K] : bitboards[k]),
        sideToMove ^ 1
    );

    int best_score = -Infinity;
    int raw_eval = ValueNone;

    if (!inCheck) {
        if (tt_res.hit && tt_res.eval != ValueNone && std::abs(tt_res.eval) < MateScore) {
            raw_eval = tt_res.eval;
        } else {
            raw_eval = evaluate();
        }
        int stand_pat = adjustEvalWithCorrHist(raw_eval);
        if (tt_res.hit && std::abs(tt_res.score) < MateScore) {
            if (tt_res.bound == hash_type_exact ||
                (tt_res.bound == hash_type_beta && tt_res.score > stand_pat) ||
                (tt_res.bound == hash_type_alpha && tt_res.score < stand_pat)) {
                stand_pat = tt_res.score;
            }
        }

        best_score = stand_pat;
        if (best_score >= beta) {
            if (!tt_res.hit) {
                TT::save(best_score, raw_eval, 0, 0, hash_type_beta, pv_node);
            }
            return best_score;
        }
        if (best_score > alpha)
            alpha = best_score;
    }

    MoveList_t moveList;
    moveList.count = 0;
    if (inCheck) {
        generateMoves(moveList);
    } else {
        generateCapturesAndPromotions(moveList);
    }

    int move_scores[256];
    for (int i = 0; i < moveList.count; ++i) {
        const int m = moveList.moves[i];
        if (m == tt_res.move) {
            move_scores[i] = 3000000;
        } else if (getMoveCapture(m) || getPromo(m)) {
            const int toSq = getMoveTarget(m);
            const int piece = getMovePiece(m);
            const int capType = getCapturedPieceType(m);
            int victim = getEp(m) ? mvv_victim_score[P]
                       : (mailbox[toSq] != NoPiece ? mvv_victim_score[capType] : 0);
            if (getPromo(m))
                victim += mvv_victim_score[pieceType(getPromo(m))];
            int cap_hist = capture_history[piece][toSq][capType];
            if (ply >= 1 && current_move[ply - 1]) {
                const int prev1 = current_move[ply - 1];
                cap_hist += capture_cont_history[getMovePiece(prev1)][getMoveTarget(prev1)][piece][toSq];
            }
            move_scores[i] = 1000000 + victim * 16 - pieceType(piece) + cap_hist / 16;
        } else {
            const int pkey = static_cast<int>((pawn_key ^ (pawn_key >> 32)) & PawnHistMask);
            move_scores[i] = history[sideToMove][getMoveSource(m)][getMoveTarget(m)]
                           + pawn_history[pkey][getMovePiece(m)][getMoveTarget(m)];
        }
    }

    int bestmove = 0;
    uint8_t hash_flag = hash_type_alpha;
    int legal_moves = 0;

    for (int count = 0; count < moveList.count; ++count) {
        const int move = pickNextMove(moveList, move_scores, count);

        if (!inCheck) {
            // Delta pruning
            int gain = 200;
            if (getEp(move)) {
                gain += see_piece_values[P];
            } else if (getMoveCapture(move)) {
                const int cap = mailbox[getMoveTarget(move)];
                if (cap != NoPiece) gain += see_piece_values[cap];
            }
            if (getPromo(move)) {
                gain += see_piece_values[getPromo(move)] - see_piece_values[P];
            }
            if (best_score + gain < alpha)
                continue;

            // SEE pruning for losing captures
            if (!see_ge(move, 0))
                continue;
        }

        const BoardSnapshot snapshot;
        if (!makeMove(move)) {
            snapshot.restore();
            continue;
        }

        Nnue::doMove(move);
        TT::prefetch(hash_key);

        legal_moves++;
        current_move[ply] = move;
        ply++;
        repetition_index++;
        repetition_table[repetition_index] = hash_key;

        const int score = -qsearch(-beta, -alpha);

        ply--;
        repetition_index--;
        snapshot.restore();
        Nnue::undoMove(move);

        if (Limits.stopped.load(std::memory_order_relaxed))
            return 0;

        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                bestmove = move;
                alpha = score;
                hash_flag = hash_type_exact;

                if (pv_node) {
                    pv_table[ply][ply] = move;
                    for (int next_ply = ply + 1; next_ply < pv_length[ply + 1]; ++next_ply)
                        pv_table[ply][next_ply] = pv_table[ply + 1][next_ply];
                    pv_length[ply] = pv_length[ply + 1];
                }

                if (score >= beta) {
                    hash_flag = hash_type_beta;
                    break;
                }
            }
        }
    }

    if (inCheck && legal_moves == 0)
        return -MateValue + ply;

    if (!Limits.stopped.load(std::memory_order_relaxed)) {
        TT::save(best_score, raw_eval, bestmove, 0, hash_flag, pv_node);
    }

    return best_score;
}

/**
 * Principal-variation alpha-beta search in negamax form.
 *
 * @param alpha,beta   search window (fail-soft)
 * @param depth        remaining depth in plies
 * @param cut_node     expected to fail high (used by reductions and IIR)
 * @param excludedMove move to skip (singular-extension verification), or 0
 * @return the score from the side-to-move's point of view
 */
int negamax(int alpha, int beta, int depth, bool cut_node, int excludedMove) {
    if ((++nodes & 1023) == 0)
        checkTime();

    if (Limits.stopped.load(std::memory_order_relaxed))
        return 0;

    if (ply > seldepth)
        seldepth = ply;

    if (!excludedMove && ply < MaxPly)
        pv_length[ply] = ply;

    if (ply && isDraw())
        return 0;

    const bool pv_node = (beta - alpha) > 1;

    // Mate distance pruning
    if (ply > 0) {
        alpha = std::max(alpha, -MateValue + ply);
        beta  = std::min(beta, MateValue - ply - 1);
        if (alpha >= beta)
            return alpha;
    }

    const bool inCheck = isSquareAttacked(
        ls1b(sideToMove == White ? bitboards[K] : bitboards[k]),
        sideToMove ^ 1
    );

    if (inCheck)
        depth++;

    if (depth <= 0 || ply >= MaxPly - 4 || repetition_index >= 1020)
        return qsearch(alpha, beta);

    // Transposition Table Probe
    TTProbeResult tt_res{false, false, no_hash_found, ValueNone, 0, 0, hash_type_none, false};
    if (!excludedMove) {
        tt_res = TT::probe_ex(alpha, beta, depth, pv_node);
        if (ply > 0 && !pv_node && tt_res.cutoff)
            return tt_res.score;
    }

    const bool tt_pv = pv_node || tt_res.tt_pv;
    int bestmove = tt_res.move;

    if (ply == 0 && multi_pv_count > 0 && bestmove) {
        for (int pvi = 0; pvi < multi_pv_count; ++pvi) {
            if (bestmove == multi_pv_moves[pvi]) {
                bestmove = 0;
                break;
            }
        }
    }

    // Syzygy Endgame Tablebase Probe
    if (ply > 0 && !pv_node && !excludedMove && TB_LARGEST > 0 &&
        countBits(occupancies[Both]) <= static_cast<int>(TB_LARGEST) &&
        fifty == 0 && !castle) {
        const unsigned res = tb_probe_wdl(
            to_fathom_bb(occupancies[White]),
            to_fathom_bb(occupancies[Black]),
            to_fathom_bb(bitboards[K] | bitboards[k]),
            to_fathom_bb(bitboards[Q] | bitboards[q]),
            to_fathom_bb(bitboards[R] | bitboards[r]),
            to_fathom_bb(bitboards[B] | bitboards[b]),
            to_fathom_bb(bitboards[N] | bitboards[n]),
            to_fathom_bb(bitboards[P] | bitboards[p]),
            to_fathom_sq(epsq),
            sideToMove == White
        );
        if (res != TB_RESULT_FAILED) {
            nodes++;
            int tb_score = 0;
            uint8_t tb_bound = hash_type_exact;
            if (res == TB_LOSS) {
                tb_score = -TBWinScore + ply;
                tb_bound = hash_type_alpha;
            } else if (res == TB_WIN) {
                tb_score = TBWinScore - ply;
                tb_bound = hash_type_beta;
            } else {
                tb_score = 0;
                tb_bound = hash_type_exact;
            }
            if (tb_bound == hash_type_exact ||
                (tb_bound == hash_type_beta && tb_score >= beta) ||
                (tb_bound == hash_type_alpha && tb_score <= alpha)) {
                TT::save(tb_score, ValueNone, 0, depth, tb_bound, tt_pv);
                return tb_score;
            }
        }
    }

    int raw_eval = ValueNone;
    int static_eval = ValueNone;
    int eval = ValueNone;

    if (inCheck) {
        static_eval_stack[ply] = ValueNone;
        goto moves_loop;
    }

    // Static evaluation with TT eval caching and Correction History
    if (excludedMove) {
        raw_eval = evaluate();
        static_eval = eval = static_eval_stack[ply];
        if (static_eval == ValueNone)
            static_eval = eval = adjustEvalWithCorrHist(raw_eval);
    } else {
        if (tt_res.hit && tt_res.eval != ValueNone && std::abs(tt_res.eval) < MateScore) {
            raw_eval = tt_res.eval;
        } else {
            raw_eval = evaluate();
        }
        static_eval = adjustEvalWithCorrHist(raw_eval);
        static_eval_stack[ply] = static_eval;
        eval = static_eval;

        if (tt_res.hit && std::abs(tt_res.score) < MateScore) {
            if (tt_res.bound == hash_type_exact ||
                (tt_res.bound == hash_type_beta && tt_res.score > eval) ||
                (tt_res.bound == hash_type_alpha && tt_res.score < eval)) {
                eval = tt_res.score;
            }
        }
    }

    {
        const bool improving = (ply >= 2 && static_eval_stack[ply - 2] != ValueNone)
            ? (static_eval > static_eval_stack[ply - 2])
            : (ply >= 4 && static_eval_stack[ply - 4] != ValueNone && static_eval > static_eval_stack[ply - 4]);

        // Clear killers for child ply
        if (ply + 1 < MaxPly) {
            killers[0][ply + 1] = 0;
            killers[1][ply + 1] = 0;
            double_extensions[ply + 1] = double_extensions[ply];
        }

        // Razoring
        if (!pv_node && !excludedMove && depth <= 3 && eval + 240 * depth <= alpha) {
            const int rscore = qsearch(alpha, alpha + 1);
            if (rscore <= alpha)
                return rscore;
        }

        // Reverse Futility Pruning (Static Null Move Pruning)
        if (!pv_node && !excludedMove && depth <= 8 && std::abs(beta) < MateScore) {
            const int rfp_margin = 70 * depth - 22 * (improving + tt_pv);
            if (eval - rfp_margin >= beta)
                return (eval + beta) / 2;
        }

        // Null Move Pruning
        if (!pv_node && !excludedMove && allowNull && ply >= nmp_min_ply &&
            depth >= 3 && eval >= beta && static_eval >= beta - 20 * depth + 170 &&
            !noMajorsOrMinors()) {

            const BoardSnapshot snapshot;
            Nnue::doNullMove();

            current_move[ply] = 0;
            ply++;
            repetition_index++;
            repetition_table[repetition_index] = hash_key;

            if (epsq != NoSq)
                hash_key ^= enpassant_keys[epsq];
            epsq = NoSq;

            sideToMove ^= 1;
            hash_key ^= side_key;
            TT::prefetch(hash_key);

            const int R = 3 + depth / 3 + std::min(3, (eval - beta) / 200);
            allowNull = false;
            int null_score = -negamax(-beta, -beta + 1, depth - R, !cut_node);
            allowNull = true;

            ply--;
            repetition_index--;
            snapshot.restore();
            Nnue::undoNullMove();

            if (Limits.stopped.load(std::memory_order_relaxed))
                return 0;

            if (null_score >= beta) {
                if (null_score >= MateScore)
                    null_score = beta;

                if (nmp_min_ply || depth < 14)
                    return null_score;

                // Verification search at high depths to avoid Zugzwang blindspots
                nmp_min_ply = ply + (3 * (depth - R)) / 4;
                const int v_score = negamax(beta - 1, beta, depth - R, false);
                nmp_min_ply = 0;

                if (v_score >= beta)
                    return null_score;
            }
        }

        // ProbCut
        if (!pv_node && !excludedMove && depth >= 5 && std::abs(beta) < MateScore) {
            const int probBeta = beta + 190;
            if (!(tt_res.hit && tt_res.depth >= depth - 3 && tt_res.score < probBeta)) {
                MoveList_t pcMoves;
                pcMoves.count = 0;
                generateCapturesAndPromotions(pcMoves);

                int pcScores[256];
                for (int i = 0; i < pcMoves.count; ++i) {
                    pcScores[i] = scoreMove(pcMoves.moves[i], bestmove);
                }

                for (int i = 0; i < pcMoves.count; ++i) {
                    const int move = pickNextMove(pcMoves, pcScores, i);
                    if (!see_ge(move, probBeta - static_eval))
                        continue;

                    const BoardSnapshot snapshot;
                    if (!makeMove(move)) {
                        snapshot.restore();
                        continue;
                    }

                    Nnue::doMove(move);
                    TT::prefetch(hash_key);

                    nodes++;
                    current_move[ply] = move;
                    ply++;
                    repetition_index++;
                    repetition_table[repetition_index] = hash_key;

                    int pcScore = -qsearch(-probBeta, -probBeta + 1);
                    if (pcScore >= probBeta) {
                        pcScore = -negamax(-probBeta, -probBeta + 1, depth - 4, !cut_node);
                    }

                    ply--;
                    repetition_index--;
                    snapshot.restore();
                    Nnue::undoMove(move);

                    if (Limits.stopped.load(std::memory_order_relaxed))
                        return 0;

                    if (pcScore >= probBeta) {
                        TT::save(pcScore, raw_eval, move, depth - 3, hash_type_beta, tt_pv);
                        return pcScore;
                    }
                }
            }
        }

        // Internal Iterative Reductions (IIR)
        if (depth >= 4 && !bestmove && !excludedMove && (pv_node || cut_node)) {
            depth--;
        }
    }

// ---- Main move loop ------------------------------------------------------
moves_loop:
    const bool improving = (!inCheck && ply >= 2 && static_eval_stack[ply - 2] != ValueNone)
        ? (static_eval > static_eval_stack[ply - 2])
        : false;

    MoveList_t moveList;
    moveList.count = 0;
    generateMoves(moveList);

    if (followPV)
        enablePVScoring(moveList);

    int move_scores[256];
    for (int i = 0; i < moveList.count; ++i) {
        move_scores[i] = scoreMove(moveList.moves[i], bestmove);
    }

    if (moveList.count > 1)
        Nnue::hintCommonParent();

    int best_score = -Infinity;
    uint8_t hash_flag = hash_type_alpha;
    int legal_moves = 0;
    int moves_searched = 0;

    int quiet_searched[64];
    int quiet_count = 0;
    int captures_searched[64];
    int capture_count = 0;

    for (int count = 0; count < moveList.count; ++count) {
        const int move = pickNextMove(moveList, move_scores, count);

        if (move == excludedMove)
            continue;

        if (ply == 0 && multi_pv_count > 0) {
            bool is_excluded = false;
            for (int pvi = 0; pvi < multi_pv_count; ++pvi) {
                if (move == multi_pv_moves[pvi]) {
                    is_excluded = true;
                    break;
                }
            }
            if (is_excluded)
                continue;
        }

        const bool is_capture = (getMoveCapture(move) != 0);
        const bool is_promo   = (getPromo(move) != 0);
        const bool is_quiet   = !is_capture && !is_promo;

        const int fromSq = getMoveSource(move);
        const int toSq   = getMoveTarget(move);
        const int piece  = getMovePiece(move);

        int move_hist = 0;
        if (is_quiet) {
            move_hist = history[sideToMove][fromSq][toSq];
            const int pkey = static_cast<int>((pawn_key ^ (pawn_key >> 32)) & PawnHistMask);
            move_hist += pawn_history[pkey][piece][toSq];
            if (ply >= 1 && current_move[ply - 1]) {
                const int prev1 = current_move[ply - 1];
                move_hist += cont_history[0][getMovePiece(prev1)][getMoveTarget(prev1)][piece][toSq];
            }
            if (ply >= 2 && current_move[ply - 2]) {
                const int prev2 = current_move[ply - 2];
                move_hist += cont_history[1][getMovePiece(prev2)][getMoveTarget(prev2)][piece][toSq];
            }
            if (ply >= 4 && current_move[ply - 4]) {
                const int prev4 = current_move[ply - 4];
                move_hist += cont_history[2][getMovePiece(prev4)][getMoveTarget(prev4)][piece][toSq];
            }
        }

        // Pre-move pruning (before makeMove and NNUE update!)
        if (ply > 0 && !pv_node && !inCheck && moves_searched > 0 && best_score > -MateScore) {
            if (is_quiet) {
                // Late Move Pruning (LMP)
                const int lmp_limit = (3 + depth * depth) / (2 - improving);
                if (depth <= 8 && moves_searched >= lmp_limit)
                    continue;

                // Futility Pruning
                const int lmr_depth = std::max(0, depth - LMRTable[std::min(depth, MaxSearchDepth - 1)][std::min(moves_searched, 63)]);
                if (lmr_depth <= 8 && eval + 110 + 95 * lmr_depth + move_hist / 64 <= alpha)
                    continue;

                // Quiet History Pruning
                if (depth <= 4 && move_hist < -3500 * depth)
                    continue;

                // Quiet SEE Pruning
                if (depth <= 8 && !see_ge(move, -60 * depth))
                    continue;
            } else {
                // Capture SEE Pruning
                if (depth <= 6 && move_scores[count] < 1000000 && !see_ge(move, -25 * depth * depth))
                    continue;
            }
        }

        // Singular Extensions
        int extension = 0;
        if (ply > 0 && depth >= 7 && move == bestmove && !excludedMove &&
            tt_res.hit && tt_res.depth >= depth - 3 &&
            (tt_res.bound == hash_type_beta || tt_res.bound == hash_type_exact) &&
            std::abs(tt_res.score) < MateScore) {

            const int sBeta = std::max(-MateScore + 1, tt_res.score - 2 * depth);
            const int sDepth = (depth - 1) / 2;

            const int sScore = negamax(sBeta - 1, sBeta, sDepth, cut_node, move);

            if (Limits.stopped.load(std::memory_order_relaxed))
                return 0;

            if (sScore < sBeta) {
                extension = 1;
                if (!pv_node && sScore < sBeta - 24 && double_extensions[ply] <= 6) {
                    extension = 2;
                    if (ply + 1 < MaxPly)
                        double_extensions[ply + 1] = double_extensions[ply] + 1;
                }
            } else if (sBeta >= beta) {
                // Multi-Cut Pruning
                return sBeta;
            } else if (tt_res.score >= beta) {
                // Negative Extension
                extension = -1;
            }
        }

        const BoardSnapshot snapshot;
        if (!makeMove(move)) {
            snapshot.restore();
            continue;
        }

        Nnue::doMove(move);
        TT::prefetch(hash_key);

        legal_moves++;
        current_move[ply] = move;
        ply++;
        repetition_index++;
        repetition_table[repetition_index] = hash_key;

        const int new_depth = depth - 1 + extension;
        int score = 0;

        // PVS: the first move gets a full window; later moves are searched with a
        // null window (and late-move reductions), then re-searched on a fail-high.
        if (moves_searched == 0) {
            score = -negamax(-beta, -alpha, new_depth, ! (pv_node || !cut_node));
        } else {
            int R = 0;
            if (depth >= 3 && moves_searched >= 1 + 2 * pv_node) {
                if (is_quiet) {
                    R = LMRTable[std::min(depth, MaxSearchDepth - 1)][std::min(moves_searched, 63)];
                    R += !tt_pv;
                    R += !improving;
                    if (cut_node) R += 1;
                    if (move == killers[0][ply - 1] || move == killers[1][ply - 1]) R -= 1;
                    R -= std::clamp(move_hist / 6144, -2, 2);
                } else {
                    R = LMRTable[std::min(depth, MaxSearchDepth - 1)][std::min(moves_searched, 63)] / 2;
                    if (!tt_pv) R += 1;
                }
            }

            const int reduced_depth = std::clamp(new_depth - R, 1, new_depth);
            score = -negamax(-alpha - 1, -alpha, reduced_depth, true);

            if (score > alpha && reduced_depth < new_depth) {
                score = -negamax(-alpha - 1, -alpha, new_depth, !cut_node);
            }

            if (score > alpha && score < beta) {
                score = -negamax(-beta, -alpha, new_depth, false);
            }
        }

        ply--;
        repetition_index--;
        snapshot.restore();
        Nnue::undoMove(move);

        if (Limits.stopped.load(std::memory_order_relaxed))
            return 0;

        moves_searched++;

        if (score > best_score) {
            best_score = score;

            if (score > alpha) {
                bestmove = move;
                alpha = score;
                hash_flag = hash_type_exact;

                if (!excludedMove && pv_node) {
                    pv_table[ply][ply] = move;
                    for (int next_ply = ply + 1; next_ply < pv_length[ply + 1]; ++next_ply)
                        pv_table[ply][next_ply] = pv_table[ply + 1][next_ply];
                    pv_length[ply] = pv_length[ply + 1];
                }

                if (score >= beta) {
                    hash_flag = hash_type_beta;

                    // Beta cutoff: reward the cutoff move and penalise the moves tried before it.
                    const int bonus = std::min(2000, 16 * depth * depth + 32 * depth + 16);

                    if (is_quiet) {
                        if (killers[0][ply] != move) {
                            killers[1][ply] = killers[0][ply];
                            killers[0][ply] = move;
                        }
                        if (ply >= 1 && current_move[ply - 1]) {
                            const int prev = current_move[ply - 1];
                            countermoves[getMovePiece(prev)][getMoveTarget(prev)] = move;
                        }

                        updateQuietHistory(move, bonus);
                        for (int q = 0; q < quiet_count; ++q) {
                            updateQuietHistory(quiet_searched[q], -bonus);
                        }
                    } else if (is_capture) {
                        updateCaptureHistory(move, bonus);
                    }

                    for (int c = 0; c < capture_count; ++c) {
                        updateCaptureHistory(captures_searched[c], -bonus);
                    }

                    break;
                }
            }
        }

        if (is_quiet && quiet_count < 64) {
            quiet_searched[quiet_count++] = move;
        } else if (is_capture && capture_count < 64) {
            captures_searched[capture_count++] = move;
        }
    }

    // No legal move: checkmate or stalemate (or a failed singular search / MultiPV exclusion).
    if (legal_moves == 0) {
        if (excludedMove || (ply == 0 && multi_pv_count > 0))
            return alpha;
        if (inCheck)
            return -MateValue + ply;
        return 0;
    }

    if (!excludedMove && !Limits.stopped.load(std::memory_order_relaxed)) {
        if (ply > 0 || multi_pv_count == 0) {
            TT::save(best_score, raw_eval, bestmove, depth, hash_flag, tt_pv);

            // Learn how far the static eval was from the search result.
            if (!inCheck &&
                !(bestmove && (getMoveCapture(bestmove) || getPromo(bestmove))) &&
                std::abs(best_score) < MateScore &&
                ((hash_flag == hash_type_exact) ||
                 (hash_flag == hash_type_beta && best_score > static_eval) ||
                 (hash_flag == hash_type_alpha && best_score < static_eval))) {
                updateCorrHist(best_score - static_eval, depth);
            }
        }
    }

    return best_score;
}

/// Helper-thread loop: plain iterative deepening (no output), staggered start depth for diversity.
void Thread::search() {
    if (MultiPV > 1)
        return;

    current_thread = &this->state;
    current_thread->reset_search_state();

    for (int curr_depth = 1 + int(idx % 2); curr_depth <= Limits.depth; ++curr_depth) {
        followPV = true;
        negamax(-Infinity, Infinity, curr_depth, false);
        if (Limits.stopped.load(std::memory_order_relaxed))
            break;
    }
}

/// Probes Syzygy tablebases at the root position using DTZ tables.
int root_tb_probe(RootTbResult &best_res, RootTbResult *all_results, int max_results) {
    if (TB_LARGEST == 0 || countBits(occupancies[Both]) > static_cast<int>(TB_LARGEST) || castle != 0)
        return 0;

    unsigned tb_moves[TB_MAX_MOVES];
    const unsigned res = tb_probe_root(
        to_fathom_bb(occupancies[White]),
        to_fathom_bb(occupancies[Black]),
        to_fathom_bb(bitboards[K] | bitboards[k]),
        to_fathom_bb(bitboards[Q] | bitboards[q]),
        to_fathom_bb(bitboards[R] | bitboards[r]),
        to_fathom_bb(bitboards[B] | bitboards[b]),
        to_fathom_bb(bitboards[N] | bitboards[n]),
        to_fathom_bb(bitboards[P] | bitboards[p]),
        fifty,
        to_fathom_sq(epsq),
        sideToMove == White,
        (all_results && max_results > 0) ? tb_moves : nullptr
    );

    if (res == TB_RESULT_FAILED || res == TB_RESULT_CHECKMATE || res == TB_RESULT_STALEMATE)
        return 0;

    const int best_from = from_fathom_sq(TB_GET_FROM(res));
    const int best_to   = from_fathom_sq(TB_GET_TO(res));
    const int best_prom = TB_GET_PROMOTES(res);
    const int best_wdl  = TB_GET_WDL(res);
    const int best_dtz  = TB_GET_DTZ(res);

    MoveList_t rootMoves;
    rootMoves.count = 0;
    generateMoves(rootMoves);

    auto match_tb_move = [&](int from, int to, int prom) -> int {
        for (int i = 0; i < rootMoves.count; ++i) {
            const int m = rootMoves.moves[i];
            if (getMoveSource(m) == from && getMoveTarget(m) == to) {
                const int p = getPromo(m);
                if (prom == 0 && p == 0) return m;
                if (prom == 1 && (p == Q || p == q)) return m;
                if (prom == 2 && (p == R || p == r)) return m;
                if (prom == 3 && (p == B || p == b)) return m;
                if (prom == 4 && (p == N || p == n)) return m;
            }
        }
        return 0;
    };

    auto is_direct_checkmate = [&](int move) -> bool {
        const BoardSnapshot snapshot;
        if (!makeMove(move)) {
            snapshot.restore();
            return false;
        }
        const int oppKingSq = ls1b(sideToMove == White ? bitboards[K] : bitboards[k]);
        if (!isSquareAttacked(oppKingSq, sideToMove ^ 1)) {
            snapshot.restore();
            return false;
        }
        MoveList_t oppMoves;
        oppMoves.count = 0;
        generateMoves(oppMoves);
        bool has_legal = false;
        for (int j = 0; j < oppMoves.count; ++j) {
            const BoardSnapshot snap2;
            if (makeMove(oppMoves.moves[j])) {
                has_legal = true;
                snap2.restore();
                break;
            }
            snap2.restore();
        }
        snapshot.restore();
        return !has_legal;
    };

    auto calc_tb_score = [&](int move, int wdl, int dtz) -> int {
        if (wdl == TB_WIN) {
            if (dtz == 1 && is_direct_checkmate(move))
                return MateValue - 1;
            return TBWinScore - dtz;
        }
        if (wdl == TB_LOSS)
            return -TBWinScore + dtz;
        if (wdl == TB_CURSED_WIN)
            return 1;
        if (wdl == TB_BLESSED_LOSS)
            return -1;
        return 0;
    };

    const int best_move = match_tb_move(best_from, best_to, best_prom);
    if (!best_move)
        return 0;

    // Verify legality
    {
        const BoardSnapshot snapshot;
        if (!makeMove(best_move)) {
            snapshot.restore();
            return 0;
        }
        snapshot.restore();
    }

    best_res.move  = best_move;
    best_res.wdl   = best_wdl;
    best_res.dtz   = best_dtz;
    best_res.score = calc_tb_score(best_move, best_wdl, best_dtz);

    if (all_results && max_results > 0) {
        int count = 0;
        for (int i = 0; tb_moves[i] != TB_RESULT_FAILED && count < max_results; ++i) {
            const int m_from = from_fathom_sq(TB_GET_FROM(tb_moves[i]));
            const int m_to   = from_fathom_sq(TB_GET_TO(tb_moves[i]));
            const int m_prom = TB_GET_PROMOTES(tb_moves[i]);
            const int m_wdl  = TB_GET_WDL(tb_moves[i]);
            const int m_dtz  = TB_GET_DTZ(tb_moves[i]);

            const int m = match_tb_move(m_from, m_to, m_prom);
            if (!m)
                continue;

            const BoardSnapshot snapshot;
            if (!makeMove(m)) {
                snapshot.restore();
                continue;
            }
            snapshot.restore();

            all_results[count].move  = m;
            all_results[count].wdl   = m_wdl;
            all_results[count].dtz   = m_dtz;
            all_results[count].score = calc_tb_score(m, m_wdl, m_dtz);
            count++;
        }

        if (count == 0) {
            all_results[0] = best_res;
            count = 1;
        } else {
            std::stable_sort(all_results, all_results + count, [](const RootTbResult &a, const RootTbResult &b) {
                return a.score > b.score;
            });
        }

        return count;
    }

    return 1;
}

/**
 * Main-thread iterative deepening: aspiration windows around the previous
 * score, MultiPV line extraction and formatting, `info` output after each
 * iteration, soft/hard time management scaled by best-move stability and
 * score drops, and the final `bestmove` report.
 */
void MainThread::search() {
    current_thread = &this->state;
    current_thread->reset_search_state();

    int score = 0;
    int prev_score = 0;
    int best_root_move = 0;
    int best_move_stability = 0;

    // Seed fallback root move with the first legal move and count legal moves
    MoveList_t rootMoves;
    rootMoves.count = 0;
    generateMoves(rootMoves);
    int legal_root_moves = 0;
    for (int i = 0; i < rootMoves.count; ++i) {
        const BoardSnapshot snapshot;
        if (makeMove(rootMoves.moves[i])) {
            if (legal_root_moves == 0)
                best_root_move = rootMoves.moves[i];
            legal_root_moves++;
        }
        snapshot.restore();
    }

    if (legal_root_moves == 0) {
        std::cout << "bestmove (none)\n" << std::flush;
        return;
    }

    const int root_multipv = std::clamp(MultiPV, 1, legal_root_moves);

    // Syzygy Endgame Tablebase Root Probe (DTZ)
    RootTbResult root_tb_res;
    RootTbResult all_tb_results[256];
    const int tb_count = root_tb_probe(root_tb_res, all_tb_results, 256);

    if (tb_count > 0) {
        Limits.stopped.store(true, std::memory_order_relaxed);

        for (Thread* th : Threads.threads) {
            if (th != this)
                th->wait_for_search_finished();
        }

        const int num_to_report = std::min(root_multipv, tb_count);
        if (num_to_report > 1) {
            for (int pvi = 0; pvi < num_to_report; ++pvi) {
                std::cout << "info depth 1 seldepth 1 multipv " << (pvi + 1) << " ";
                print_score(std::cout, all_tb_results[pvi].score);
                std::cout << "nodes 1 nps 1000 tbhits 1 time 0 pv ";
                printMove(all_tb_results[pvi].move);
                std::cout << "\n";
            }
            std::cout << std::flush;
        } else {
            std::cout << "info depth 1 seldepth 1 ";
            print_score(std::cout, root_tb_res.score);
            std::cout << "nodes 1 nps 1000 tbhits 1 time 0 pv ";
            printMove(root_tb_res.move);
            std::cout << "\n" << std::flush;
        }

        TT::save(root_tb_res.score, ValueNone, root_tb_res.move, 128, hash_type_exact, true);

        if (Limits.ponder.load(std::memory_order_relaxed)) {
            while (Limits.ponder.load(std::memory_order_relaxed) && !Limits.stopped.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        std::cout << "bestmove ";
        printMove(root_tb_res.move);
        std::cout << "\n" << std::flush;
        return;
    }

    struct RootPVInfo {
        int move = 0;
        int score = 0;
        int selective_depth = 0;
        int line_length = 0;
        int line[MaxPly] = {0};
    };
    RootPVInfo root_pv_lines[256];
    std::memset(multi_pv_moves, 0, sizeof(multi_pv_moves));
    multi_pv_count = 0;

    for (int curr_depth = 1; curr_depth <= Limits.depth; ++curr_depth) {
        const int prev_best_move = best_root_move;
        bool iteration_aborted = false;

        for (int pvi = 0; pvi < root_multipv; ++pvi) {
            multi_pv_count = pvi;
            seldepth = 0;

            bool can_follow_pv = false;
            if (curr_depth > 1 && root_pv_lines[pvi].line_length > 0) {
                bool already_used = false;
                for (int k = 0; k < pvi; ++k) {
                    if (root_pv_lines[pvi].line[0] == multi_pv_moves[k]) {
                        already_used = true;
                        break;
                    }
                }
                if (!already_used) {
                    pv_length[0] = root_pv_lines[pvi].line_length;
                    for (int k = 0; k < root_pv_lines[pvi].line_length; ++k)
                        pv_table[0][k] = root_pv_lines[pvi].line[k];
                    can_follow_pv = true;
                }
            }
            if (!can_follow_pv) {
                pv_length[0] = 0;
                pv_table[0][0] = 0;
            }

            int line_score = 0;

            if (root_multipv == 1) {
                int delta = 16;
                int alpha = -Infinity;
                int beta  = Infinity;

                if (curr_depth >= 5) {
                    alpha = std::max(-Infinity, score - delta);
                    beta  = std::min(Infinity, score + delta);
                }

                int asp_depth = curr_depth;

                while (true) {
                    followPV = true;
                    const int iter_score = negamax(alpha, beta, asp_depth, false);

                    if (Limits.stopped.load(std::memory_order_relaxed))
                        break;

                    line_score = iter_score;

                    if (line_score <= alpha) {
                        beta = (alpha + beta) / 2;
                        alpha = std::max(-Infinity, line_score - delta);
                        asp_depth = curr_depth;
                    } else if (line_score >= beta) {
                        beta = std::min(Infinity, line_score + delta);
                        if (asp_depth > 1 && std::abs(line_score) < MateScore)
                            asp_depth--;
                    } else {
                        if (pv_table[0][0])
                            best_root_move = pv_table[0][0];
                        break;
                    }

                    delta += delta / 2 + 4;
                    if (delta > 500) {
                        alpha = -Infinity;
                        beta  = Infinity;
                    }
                }
            } else {
                followPV = can_follow_pv;
                line_score = negamax(-Infinity, Infinity, curr_depth, false);
            }

            if (Limits.stopped.load(std::memory_order_relaxed)) {
                iteration_aborted = true;
                break;
            }

            root_pv_lines[pvi].move = pv_table[0][0];
            root_pv_lines[pvi].score = line_score;
            root_pv_lines[pvi].selective_depth = seldepth;
            root_pv_lines[pvi].line_length = pv_length[0];
            for (int k = 0; k < pv_length[0]; ++k)
                root_pv_lines[pvi].line[k] = pv_table[0][k];

            multi_pv_moves[pvi] = pv_table[0][0];

            if (pvi == 0 && pv_table[0][0]) {
                best_root_move = pv_table[0][0];
                score = line_score;
            }
        }

        multi_pv_count = 0;

        if (iteration_aborted || Limits.stopped.load(std::memory_order_relaxed))
            break;

        if (root_multipv > 1) {
            std::stable_sort(root_pv_lines, root_pv_lines + root_multipv, [](const RootPVInfo& a, const RootPVInfo& b) {
                return a.score > b.score;
            });
            if (root_pv_lines[0].move)
                best_root_move = root_pv_lines[0].move;
            score = root_pv_lines[0].score;
        }

        const uint64_t total_nodes = Threads.nodes_searched();
        const uint64_t elapsed_ms  = std::max<uint64_t>(1, getTimeMs() - Limits.starttime);
        const uint64_t nps         = (total_nodes * 1000ULL) / elapsed_ms;

        if (root_multipv > 1) {
            for (int pvi = 0; pvi < root_multipv; ++pvi) {
                std::cout << "info depth " << curr_depth
                          << " seldepth " << root_pv_lines[pvi].selective_depth
                          << " multipv " << (pvi + 1)
                          << " ";
                print_score(std::cout, root_pv_lines[pvi].score);
                std::cout << "nodes " << total_nodes
                          << " nps " << nps
                          << " time " << elapsed_ms
                          << " hashfull " << TT::hashfull()
                          << " pv ";
                for (int count = 0; count < root_pv_lines[pvi].line_length; ++count) {
                    printMove(root_pv_lines[pvi].line[count]);
                    std::cout << " ";
                }
                std::cout << "\n";
            }
            std::cout << std::flush;
        } else {
            std::cout << "info depth " << curr_depth
                      << " seldepth " << seldepth
                      << " ";
            print_score(std::cout, score);
            std::cout << "nodes " << total_nodes
                      << " nps " << nps
                      << " time " << elapsed_ms
                      << " hashfull " << TT::hashfull()
                      << " pv ";
            for (int count = 0; count < pv_length[0]; ++count) {
                printMove(pv_table[0][count]);
                std::cout << " ";
            }
            std::cout << "\n" << std::flush;
        }

        // Soft time management scaling based on best-move stability and score drop
        if (Limits.timeset && curr_depth >= 5 && !Limits.ponder.load(std::memory_order_relaxed)) {
            if (best_root_move == prev_best_move) {
                best_move_stability = std::min(10, best_move_stability + 1);
            } else {
                best_move_stability = 0;
            }

            static constexpr double stability_scale[11] = {
                1.45, 1.25, 1.10, 0.98, 0.90, 0.84, 0.78, 0.74, 0.70, 0.68, 0.65
            };
            double time_factor = stability_scale[best_move_stability];

            if (curr_depth > 5 && score < prev_score - 20) {
                time_factor *= std::min(1.50, 1.0 + double(prev_score - score) / 120.0);
            }

            const uint64_t base_opt = Limits.opt_stoptime - Limits.starttime;
            const uint64_t scaled_opt = Limits.starttime + static_cast<uint64_t>(base_opt * time_factor);
            const uint64_t effective_opt = std::min(scaled_opt, Limits.max_stoptime);

            if (getTimeMs() >= effective_opt) {
                Limits.stopped.store(true, std::memory_order_relaxed);
                break;
            }
        }

        prev_score = score;
    }

    if (Limits.ponder.load(std::memory_order_relaxed)) {
        while (Limits.ponder.load(std::memory_order_relaxed) && !Limits.stopped.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    Limits.stopped.store(true, std::memory_order_relaxed);

    for (Thread* th : Threads.threads) {
        if (th != this)
            th->wait_for_search_finished();
    }

    int best_move = best_root_move ? best_root_move : pv_table[0][0];
    if (!best_move && rootMoves.count > 0)
        best_move = rootMoves.moves[0];

    int ponder_move = 0;
    if (root_multipv > 1 || root_pv_lines[0].line_length > 0) {
        if (root_pv_lines[0].line_length > 1 && root_pv_lines[0].line[0] == best_move)
            ponder_move = root_pv_lines[0].line[1];
    }
    if (!ponder_move && pv_length[0] > 1 && pv_table[0][0] == best_move) {
        ponder_move = pv_table[0][1];
    }

    std::cout << "bestmove ";
    printMove(best_move);
    if (ponder_move) {
        std::cout << " ponder ";
        printMove(ponder_move);
    }
    std::cout << "\n" << std::flush;
}

/// Starts a search on all threads using the current global Limits.
void search() {
    Threads.start_thinking();
}
