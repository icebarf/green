#ifndef ICE_GTHREADS_H
#define ICE_GTHREADS_H

#include <cstdint>

namespace Green {

struct Context
{
  uint64_t rsp;
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t rbx;
  uint64_t rbp;
};

enum class State
{
  Unused,
  Running,
  Ready,
};

struct Thread
{
  Context ctx;
  State state;
};

}

#define ICE_GTHREDS_IMPL // <-- Get rid of this line

#ifdef ICE_GTHREDS_IMPL
#undef ICE_GTHREADS_IMPL

#endif

#endif