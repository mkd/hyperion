/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#include "position.h"
#include "bitboard.h"
#include "search.h"
#include "tt.h"
#ifdef USE_NEW_NNUE
#include "stockfish_probe/nnue_incremental.h"
#endif

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace std;

void resetBoard() {
    std::memset(bitboards, 0, sizeof(bitboards));
    std::memset(occupancies, 0, sizeof(occupancies));
    for (int i = 0; i < 64; ++i)
        mailbox[i] = NoPiece;

    sideToMove = White;
    epsq = NoSq;
    castle = 0;
    flip = false;
    repetition_index = 0;
    root_repetition_index = 0;
    hash_key = 0ULL;
    pawn_key = 0ULL;
    non_pawn_key[0] = 0ULL;
    non_pawn_key[1] = 0ULL;
    fifty = 0;
    std::memset(repetition_table, 0, sizeof(repetition_table));
}

bool setPosition(const string &fenStr) {
    unsigned char col, row, token;
    int sq = a8;
    istringstream ss(fenStr);
    ss >> noskipws;

    resetBoard();

    int rank = 0, file = 0;
    while ((ss >> token) && !isspace(token)) {
        if (isdigit(token)) {
            file += (token - '0');
            sq += (token - '0');
        } else if (token == '/') {
            rank++;
            file = 0;
            sq = rank * 8 + file;
        } else {
            int p = charToPiece(token);
            if (p != NoPiece && rank < 8 && file < 8) {
                sq = rank * 8 + file;
                setBit(bitboards[p], sq);
                mailbox[sq] = p;
                sq++;
                file++;
            }
        }
    }

    ss >> token;
    sideToMove = (token == 'w' ? White : Black);
    ss >> token;

    while ((ss >> token) && !isspace(token)) {
        switch (token) {
            case 'K': castle |= wk; break;
            case 'Q': castle |= wq; break;
            case 'k': castle |= bk; break;
            case 'q': castle |= bq; break;
            case '-': break;
        }
    }

    if (((ss >> col) && (col >= 'a' && col <= 'h')) &&
        ((ss >> row) && (row == (sideToMove == White ? '6' : '3')))) {
        int f = col - 'a';
        int r = 8 - (row - '0');
        if (((sideToMove == White) && (r == 2)) ||
            ((sideToMove == Black) && (r == 5))) {
            epsq = r * 8 + f;
        }
    } else {
        epsq = NoSq;
    }

    ss >> skipws >> fifty;

    for (int piece = P; piece <= K; piece++)
        occupancies[White] |= bitboards[piece];

    for (int piece = p; piece <= k; piece++)
        occupancies[Black] |= bitboards[piece];

    occupancies[Both] = occupancies[White] | occupancies[Black];

    hash_key = generateHashkey();
    pawn_key = generatePawnKey();
    non_pawn_key[White] = generateNonPawnKey(White);
    non_pawn_key[Black] = generateNonPawnKey(Black);

    if (!bitboards[K] || !bitboards[k]) {
        std::cout << "Error: Invalid FEN parsed. Missing King(s)." << std::endl;
        return false;
    }

#ifdef USE_NEW_NNUE
    Stockfish::Incremental::setup_reset(getFEN());
#endif

    return true;
}

string getFEN() {
    ostringstream ss;
    for (int rank = 0; rank <= 7; ++rank) {
        int emptyCnt = 0;
        for (int file = 0; file <= 7; ++file) {
            int piece = mailbox[rank * 8 + file];
            if (piece == NoPiece) {
                ++emptyCnt;
            } else {
                if (emptyCnt) {
                    ss << emptyCnt;
                    emptyCnt = 0;
                }
                ss << PieceStr[piece];
            }
        }
        if (emptyCnt)
            ss << emptyCnt;
        if (rank < 7)
            ss << '/';
    }

    ss << (sideToMove == White ? " w " : " b ");

    if (castle & wk) ss << 'K';
    if (castle & wq) ss << 'Q';
    if (castle & bk) ss << 'k';
    if (castle & bq) ss << 'q';
    if (!castle) ss << '-';

    ss << " ";
    if (epsq == NoSq)
        ss << "-";
    else
        ss << SquareToCoordinates[epsq];

    ss << " " << fifty << " " << 1 + (ply - (sideToMove == Black)) / 2;
    return ss.str();
}

void printBoard() {
    cout << endl << endl;
    cout << "    +----+----+----+----+----+----+----+----+" << endl;

    for (int rank = 0; rank < 8; rank++) {
        if (flip)
            cout << setw(3) << rank + 1 << " |";
        else
            cout << setw(3) << 8 - rank << " |";

        for (int file = 0; file < 8; file++) {
            int square = flip ? (((7 - rank) * 8) + (7 - file)) : ((rank * 8) + file);
            int piece = mailbox[square];
            Side color = (piece == NoPiece) ? White : colorFromPiece(piece);

            cout << " " << ((piece == NoPiece) ? " " : PieceStr[piece]);
            cout << ((color == White) ? " " : "*") << " |";
        }
        cout << endl << "    +----+----+----+----+----+----+----+----+" << endl;
    }

    cout << "      a    b    c    d    e    f    g    h" << endl << endl;
    cout << "  Fen:    " << getFEN() << endl;
    cout << "  Key:    " << hex << uppercase << hash_key << endl;
    cout << "  Side:   " << ((sideToMove == White) ? "White" : "Black") << endl;
    cout << "  Epsq:   " << ((epsq != NoSq) ? SquareToCoordinates[epsq] : "-") << endl;
    cout << "  Castle: " << ((castle & wk) ? "K" : "-")
         << ((castle & wq) ? "Q" : "-") << ((castle & bk) ? "k" : "-")
         << ((castle & bq) ? "q" : "-") << endl;
    cout << resetiosflags(std::cout.flags()) << endl;
}
