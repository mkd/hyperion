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
 * @file position.cpp
 * @brief FEN parsing/printing, board reset and the human-readable board dump.
 */

#include "position.h"

#include "bitboard.h"
#include "search.h"
#include "tt.h"
#include "nnue.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

using std::cout, std::endl;

void resetBoard() {
    std::memset(bitboards, 0, sizeof(bitboards));
    std::memset(occupancies, 0, sizeof(occupancies));
    for (int i = 0; i < 64; ++i)
        mailbox[i] = NoPiece;

    sideToMove            = White;
    epsq                  = NoSq;
    castle                = 0;
    flip                  = false;
    repetition_index      = 0;
    root_repetition_index = 0;
    hash_key              = 0ULL;
    pawn_key              = 0ULL;
    non_pawn_key[0]       = 0ULL;
    non_pawn_key[1]       = 0ULL;
    fifty                 = 0;
    std::memset(repetition_table, 0, sizeof(repetition_table));
}

bool setPosition(std::string_view fenView) {
    unsigned char col = 0, row = 0, token = 0;
    std::istringstream ss{std::string(fenView)};
    ss >> std::noskipws;

    resetBoard();

    // 1. Piece placement (rank 8 first, '/' separated, digits = empty squares)
    int rank = 0, file = 0;
    int sq = a8;
    while ((ss >> token) && !isspace(token)) {
        if (isdigit(token)) {
            file += (token - '0');
            sq   += (token - '0');
        } else if (token == '/') {
            rank++;
            file = 0;
            sq   = rank * 8 + file;
        } else {
            const int piece = charToPiece(token);
            if (piece != NoPiece && rank < 8 && file < 8) {
                sq = rank * 8 + file;
                setBit(bitboards[piece], sq);
                mailbox[sq] = piece;
                sq++;
                file++;
            }
        }
    }

    // 2. Side to move
    ss >> token;
    sideToMove = (token == 'w' ? White : Black);
    ss >> token;

    // 3. Castling rights
    while ((ss >> token) && !isspace(token)) {
        switch (token) {
            case 'K': castle |= wk; break;
            case 'Q': castle |= wq; break;
            case 'k': castle |= bk; break;
            case 'q': castle |= bq; break;
            default:  break;
        }
    }

    // 4. En-passant target square (only accepted on the correct rank for the side to move)
    if (((ss >> col) && (col >= 'a' && col <= 'h')) &&
        ((ss >> row) && (row == (sideToMove == White ? '6' : '3')))) {
        const int f = col - 'a';
        const int r = 8 - (row - '0');
        if (((sideToMove == White) && (r == 2)) ||
            ((sideToMove == Black) && (r == 5))) {
            epsq = r * 8 + f;
        }
    } else {
        epsq = NoSq;
    }

    // 5. Half-move clock (the full-move number is not tracked)
    ss >> std::skipws >> fifty;

    // Derive occupancies and Zobrist keys from the piece bitboards.
    for (int piece = P; piece <= K; piece++)
        occupancies[White] |= bitboards[piece];
    for (int piece = p; piece <= k; piece++)
        occupancies[Black] |= bitboards[piece];
    occupancies[Both] = occupancies[White] | occupancies[Black];

    hash_key             = generateHashkey();
    pawn_key             = generatePawnKey();
    non_pawn_key[White]  = generateNonPawnKey(White);
    non_pawn_key[Black]  = generateNonPawnKey(Black);

    if (!bitboards[K] || !bitboards[k]) {
        cout << "Error: Invalid FEN parsed. Missing King(s)." << endl;
        return false;
    }

    Nnue::setupReset(getFEN());

    return true;
}

std::string getFEN() {
    std::ostringstream ss;

    for (int rank = 0; rank <= 7; ++rank) {
        int emptyCount = 0;
        for (int file = 0; file <= 7; ++file) {
            const int piece = mailbox[rank * 8 + file];
            if (piece == NoPiece) {
                ++emptyCount;
                continue;
            }
            if (emptyCount) {
                ss << emptyCount;
                emptyCount = 0;
            }
            ss << PieceStr[piece];
        }
        if (emptyCount)
            ss << emptyCount;
        if (rank < 7)
            ss << '/';
    }

    ss << (sideToMove == White ? " w " : " b ");

    if (castle & wk) ss << 'K';
    if (castle & wq) ss << 'Q';
    if (castle & bk) ss << 'k';
    if (castle & bq) ss << 'q';
    if (!castle)     ss << '-';

    ss << ' ';
    if (epsq == NoSq)
        ss << '-';
    else
        ss << SquareToCoordinates[epsq];

    ss << ' ' << fifty << ' ' << (1 + (ply - (sideToMove == Black)) / 2);
    return ss.str();
}

void printBoard() {
    using std::setw;

    cout << endl << endl;
    cout << "    +----+----+----+----+----+----+----+----+" << endl;

    for (int rank = 0; rank < 8; rank++) {
        if (flip)
            cout << setw(3) << rank + 1 << " |";
        else
            cout << setw(3) << 8 - rank << " |";

        for (int file = 0; file < 8; file++) {
            const int square = flip ? (((7 - rank) * 8) + (7 - file)) : ((rank * 8) + file);
            const int piece  = mailbox[square];
            const Side color = (piece == NoPiece) ? White : colorFromPiece(piece);

            cout << " " << ((piece == NoPiece) ? " " : PieceStr[piece]);
            cout << ((color == White) ? " " : "*") << " |";
        }
        cout << endl << "    +----+----+----+----+----+----+----+----+" << endl;
    }

    cout << "      a    b    c    d    e    f    g    h" << endl << endl;
    cout << "  Fen:    " << getFEN() << endl;
    cout << "  Key:    " << std::hex << std::uppercase << hash_key << endl;
    cout << "  Side:   " << ((sideToMove == White) ? "White" : "Black") << endl;
    cout << "  Epsq:   " << ((epsq != NoSq) ? SquareToCoordinates[epsq] : "-") << endl;
    cout << "  Castle: " << ((castle & wk) ? "K" : "-")
         << ((castle & wq) ? "Q" : "-") << ((castle & bk) ? "k" : "-")
         << ((castle & bq) ? "q" : "-") << endl;
    cout << std::resetiosflags(cout.flags()) << endl;
}
