/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
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

using namespace std;

Limits_t Limits;

static int LMRTable[MaxSearchDepth][64];

static constexpr int see_piece_values[13] = {
    100, 300, 310, 500, 950, 20000,
    100, 300, 310, 500, 950, 20000,
    0
};

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

static inline void checkTime() noexcept {
    if (Limits.timeset && getTimeMs() >= Limits.max_stoptime) {
        Limits.stopped.store(true, std::memory_order_relaxed);
    }
}

static inline void updateHistoryStat(int &entry, int bonus) noexcept {
    const int clamped = std::clamp(bonus, -MaxHistory, MaxHistory);
    entry += clamped - entry * std::abs(clamped) / MaxHistory;
}

static inline void updateQuietHistory(int move, int bonus) noexcept {
    const int from  = getMoveSource(move);
    const int to    = getMoveTarget(move);
    const int piece = getMovePiece(move);

    updateHistoryStat(history[sideToMove][from][to], bonus);

    if (ply >= 1 && current_move[ply - 1]) {
        const int prev1 = current_move[ply - 1];
        updateHistoryStat(cont_history[getMovePiece(prev1)][getMoveTarget(prev1)][piece][to], bonus);
    }
    if (ply >= 2 && current_move[ply - 2]) {
        const int prev2 = current_move[ply - 2];
        updateHistoryStat(cont_history[getMovePiece(prev2)][getMoveTarget(prev2)][piece][to], bonus);
    }
}

static inline int getCapturedPieceType(int move) noexcept {
    if (getEp(move)) return P;
    const int cap = mailbox[getMoveTarget(move)];
    return (cap != NoPiece) ? pieceType(cap) : P;
}

static inline void updateCaptureHistory(int move, int bonus) noexcept {
    const int piece   = getMovePiece(move);
    const int to      = getMoveTarget(move);
    const int capType = getCapturedPieceType(move);
    updateHistoryStat(capture_history[piece][to][capType], bonus);
}

static inline Bitboard getSmallestAttacker(int sq, int side, int &attackerPiece) noexcept {
    if (side == White) {
        if (Bitboard bb = PawnAttacks[Black][sq] & bitboards[P]) {
            attackerPiece = P;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = KnightAttacks[sq] & bitboards[N]) {
            attackerPiece = N;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = getBishopAttacks(sq, occupancies[Both]) & bitboards[B]) {
            attackerPiece = B;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = getRookAttacks(sq, occupancies[Both]) & bitboards[R]) {
            attackerPiece = R;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = (getBishopAttacks(sq, occupancies[Both]) | getRookAttacks(sq, occupancies[Both])) & bitboards[Q]) {
            attackerPiece = Q;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = KingAttacks[sq] & bitboards[K]) {
            attackerPiece = K;
            return SqBB[ls1b(bb)];
        }
    } else {
        if (Bitboard bb = PawnAttacks[White][sq] & bitboards[p]) {
            attackerPiece = p;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = KnightAttacks[sq] & bitboards[n]) {
            attackerPiece = n;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = getBishopAttacks(sq, occupancies[Both]) & bitboards[b]) {
            attackerPiece = b;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = getRookAttacks(sq, occupancies[Both]) & bitboards[r]) {
            attackerPiece = r;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = (getBishopAttacks(sq, occupancies[Both]) | getRookAttacks(sq, occupancies[Both])) & bitboards[q]) {
            attackerPiece = q;
            return SqBB[ls1b(bb)];
        }
        if (Bitboard bb = KingAttacks[sq] & bitboards[k]) {
            attackerPiece = k;
            return SqBB[ls1b(bb)];
        }
    }
    return 0ULL;
}

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

        const int cap_hist = capture_history[piece][toSq][cap_type];
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
    if (ply >= 1 && current_move[ply - 1]) {
        const int prev1 = current_move[ply - 1];
        h += cont_history[getMovePiece(prev1)][getMoveTarget(prev1)][piece][toSq];
    }
    if (ply >= 2 && current_move[ply - 2]) {
        const int prev2 = current_move[ply - 2];
        h += cont_history[getMovePiece(prev2)][getMoveTarget(prev2)][piece][toSq];
    }

    return std::clamp(h, -400000, 750000);
}

static inline int pickNextMove(MoveList_t &moveList, int *scores, int startIdx) noexcept {
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

static inline void enablePVScoring(MoveList_t &moveList) noexcept {
    followPV = false;
    for (int i = 0; i < moveList.count; ++i) {
        if (pv_table[0][ply] == moveList.moves[i]) {
            scorePV = true;
            followPV = true;
            break;
        }
    }
}

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
            move_scores[i] = 1000000 + victim * 16 - pieceType(piece) + capture_history[piece][toSq][capType] / 16;
        } else {
            move_scores[i] = history[sideToMove][getMoveSource(m)][getMoveTarget(m)];
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

        saveBoard();
        if (!makeMove(move)) {
            takeBack(move);
            continue;
        }

        NNUE_DO(move);
        TT::prefetch(hash_key);

        legal_moves++;
        current_move[ply] = move;
        ply++;
        repetition_index++;
        repetition_table[repetition_index] = hash_key;

        const int score = -qsearch(-beta, -alpha);

        ply--;
        repetition_index--;
        takeBack(move);
        NNUE_UNDO(move);

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

    // Syzygy Endgame Tablebase Probe
    if (ply > 0 && !pv_node && !excludedMove && TB_LARGEST > 0 &&
        countBits(occupancies[Both]) <= static_cast<int>(TB_LARGEST) &&
        fifty == 0 && !castle) {
        const unsigned res = tb_probe_wdl(
            occupancies[White], occupancies[Black],
            bitboards[K] | bitboards[k],
            bitboards[Q] | bitboards[q],
            bitboards[R] | bitboards[r],
            bitboards[B] | bitboards[b],
            bitboards[N] | bitboards[n],
            bitboards[P] | bitboards[p],
            epsq == NoSq ? 0 : epsq, sideToMove == White
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

            saveBoard();
            NNUE_DO_NULL;

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
            takeBack(0);
            NNUE_UNDO_NULL;

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

                    saveBoard();
                    if (!makeMove(move)) {
                        takeBack(move);
                        continue;
                    }

                    NNUE_DO(move);
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
                    takeBack(move);
                    NNUE_UNDO(move);

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

#ifdef USE_NEW_NNUE
    if (moveList.count > 1) {
        Stockfish::Incremental::hint_common_parent();
    }
#endif

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

        const bool is_capture = (getMoveCapture(move) != 0);
        const bool is_promo   = (getPromo(move) != 0);
        const bool is_quiet   = !is_capture && !is_promo;

        const int fromSq = getMoveSource(move);
        const int toSq   = getMoveTarget(move);
        const int piece  = getMovePiece(move);

        int move_hist = 0;
        if (is_quiet) {
            move_hist = history[sideToMove][fromSq][toSq];
            if (ply >= 1 && current_move[ply - 1]) {
                const int prev1 = current_move[ply - 1];
                move_hist += cont_history[getMovePiece(prev1)][getMoveTarget(prev1)][piece][toSq];
            }
            if (ply >= 2 && current_move[ply - 2]) {
                const int prev2 = current_move[ply - 2];
                move_hist += cont_history[getMovePiece(prev2)][getMoveTarget(prev2)][piece][toSq];
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

        saveBoard();
        if (!makeMove(move)) {
            takeBack(move);
            continue;
        }

        NNUE_DO(move);
        TT::prefetch(hash_key);

        legal_moves++;
        current_move[ply] = move;
        ply++;
        repetition_index++;
        repetition_table[repetition_index] = hash_key;

        const int new_depth = depth - 1 + extension;
        int score = 0;

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
        takeBack(move);
        NNUE_UNDO(move);

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

    if (legal_moves == 0) {
        if (excludedMove)
            return alpha;
        if (inCheck)
            return -MateValue + ply;
        return 0;
    }

    if (!excludedMove && !Limits.stopped.load(std::memory_order_relaxed)) {
        TT::save(best_score, raw_eval, bestmove, depth, hash_flag, tt_pv);

        if (!inCheck &&
            !(bestmove && (getMoveCapture(bestmove) || getPromo(bestmove))) &&
            std::abs(best_score) < MateScore &&
            ((hash_flag == hash_type_exact) ||
             (hash_flag == hash_type_beta && best_score > static_eval) ||
             (hash_flag == hash_type_alpha && best_score < static_eval))) {
            updateCorrHist(best_score - static_eval, depth);
        }
    }

    return best_score;
}

void Thread::search() {
    current_thread = &this->state;
    current_thread->reset_search_state();

    for (int curr_depth = 1 + int(idx % 2); curr_depth <= Limits.depth; ++curr_depth) {
        followPV = true;
        negamax(-Infinity, Infinity, curr_depth, false);
        if (Limits.stopped.load(std::memory_order_relaxed))
            break;
    }
}

void MainThread::search() {
    current_thread = &this->state;
    current_thread->reset_search_state();

    int score = 0;
    int prev_score = 0;
    int best_root_move = 0;
    int best_move_stability = 0;

    // Seed fallback root move with the first legal move in case of instant timeout
    {
        MoveList_t rootMoves;
        rootMoves.count = 0;
        generateMoves(rootMoves);
        for (int i = 0; i < rootMoves.count; ++i) {
            saveBoard();
            if (makeMove(rootMoves.moves[i])) {
                best_root_move = rootMoves.moves[i];
                takeBack(rootMoves.moves[i]);
                break;
            }
            takeBack(rootMoves.moves[i]);
        }
    }

    for (int curr_depth = 1; curr_depth <= Limits.depth; ++curr_depth) {
        seldepth = 0;
        const int prev_best_move = best_root_move;

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

            score = iter_score;

            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(-Infinity, score - delta);
                asp_depth = curr_depth;
            } else if (score >= beta) {
                beta = std::min(Infinity, score + delta);
                if (asp_depth > 1 && std::abs(score) < MateScore)
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

        if (Limits.stopped.load(std::memory_order_relaxed))
            break;

        const uint64_t total_nodes = Threads.nodes_searched();
        const uint64_t elapsed_ms  = std::max<uint64_t>(1, getTimeMs() - Limits.starttime);
        const uint64_t nps         = (total_nodes * 1000ULL) / elapsed_ms;

        if (score > -MateValue && score < -MateScore) {
            std::cout << "info score mate " << -(score + MateValue) / 2 - 1
                      << " depth " << curr_depth
                      << " seldepth " << seldepth
                      << " nodes " << total_nodes
                      << " nps " << nps
                      << " time " << elapsed_ms
                      << " hashfull " << TT::hashfull()
                      << " pv ";
        } else if (score > MateScore && score < MateValue) {
            std::cout << "info score mate " << (MateValue - score) / 2 + 1
                      << " depth " << curr_depth
                      << " seldepth " << seldepth
                      << " nodes " << total_nodes
                      << " nps " << nps
                      << " time " << elapsed_ms
                      << " hashfull " << TT::hashfull()
                      << " pv ";
        } else {
            std::cout << "info score cp " << score
                      << " depth " << curr_depth
                      << " seldepth " << seldepth
                      << " nodes " << total_nodes
                      << " nps " << nps
                      << " time " << elapsed_ms
                      << " hashfull " << TT::hashfull()
                      << " pv ";
        }

        for (int count = 0; count < pv_length[0]; ++count) {
            printMove(pv_table[0][count]);
            std::cout << " ";
        }
        std::cout << "\n" << std::flush;

        // Soft time management scaling based on best-move stability and score drop
        if (Limits.timeset && curr_depth >= 5) {
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

    Limits.stopped.store(true, std::memory_order_relaxed);

    for (Thread* th : Threads.threads) {
        if (th != this)
            th->wait_for_search_finished();
    }

    std::cout << "bestmove ";
    if (best_root_move)
        printMove(best_root_move);
    else
        printMove(pv_table[0][0]);
    std::cout << "\n" << std::flush;
}

void search() {
    Threads.start_thinking();
}
