/* -*- mode: C++; indent-tabs-mode: nil; -*-
 *
 * This file is a part of LEMON, a generic C++ optimization library.
 *
 * Copyright (C) 2003-2013
 * Egervary Jeno Kombinatorikus Optimalizalasi Kutatocsoport
 * (Egervary Research Group on Combinatorial Optimization, EGRES).
 *
 * Permission to use, modify and distribute this software is granted
 * provided that this copyright notice appears in all copies. For
 * precise terms see the accompanying LICENSE file.
 *
 * This software is provided "AS IS" with no warranty of any kind,
 * express or implied, and with no claim as to its suitability for any
 * purpose.
 *
 */

#ifndef LEMON_BITS_WINDOWS_H
#define LEMON_BITS_WINDOWS_H

#include <lemon/config.h>
#include <lemon/export.h>
#include <string>

namespace lemon {
  namespace bits {
    LEMON_EXPORT
    void getWinProcTimes(double &rtime,
                         double &utime, double &stime,
                         double &cutime, double &cstime);
    LEMON_EXPORT
    std::string getWinFormattedDate();
    LEMON_EXPORT
    int getWinRndSeed();


    class LEMON_EXPORT WinLock {
    public:
      WinLock();
      ~WinLock();
      void lock();
      void unlock();
    private:
      void *_repr;
    };
  }
}

#endif
