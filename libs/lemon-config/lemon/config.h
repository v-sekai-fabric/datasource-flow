#ifndef LEMON_CONFIG_H
#define LEMON_CONFIG_H

#define LEMON_VERSION "1.3.1"

#define LEMON_HAVE_LONG_LONG 1
#define LEMON_CXX11 1

#ifdef _WIN32
#define LEMON_WIN32 1
#define LEMON_USE_WIN32_THREADS 1
#else
#define LEMON_USE_PTHREAD 1
#endif

#define LEMON_CPLEX_ 1
#define LEMON_CLP_ 2
#define LEMON_GLPK_ 3
#define LEMON_SOPLEX_ 4
#define LEMON_CBC_ 5

#endif
