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
 * @file crash_recovery.h
 * @brief Thread-local state used to recover from faults while probing Syzygy
 *        tablebase files (POSIX only).
 */

#ifndef CRASH_RECOVERY_H
#define CRASH_RECOVERY_H

#ifndef _WIN32
#include <csetjmp>

/// True while the current thread is inside a tablebase probe.
extern thread_local bool in_syzygy_probe;

/// Jump target used to abandon a probe that faulted.
extern thread_local sigjmp_buf syzygy_jmp_buf;
#endif

#endif // CRASH_RECOVERY_H
