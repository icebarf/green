/*
  Copyright (c) 2025, Amritpal Singh
  This file is part of Green.
  Green is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.

  Green is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  Green. If not, see <https://www.gnu.org/licenses/>.
*/

/*
Speacial thanks to Quentin Carbonneaux for their public domain work on gthreads.
Links: https://github.com/mpu/gthreads (see branch code0)
       https://c9x.me/articles/gthreads/intro.html
*/

/*
This C++ reiteration merely supports a nicer interface to passing a few types of
arguments and that is all. It was mostly a test/tinkering exercise for myself.
*/

#ifndef ICE_GREEN_THREADS_H
#define ICE_GREEN_THREADS_H

#include <cstdint>
#include <stack>

constexpr int max_gthreads = 4;
constexpr int stack_size = 0x400000;

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
  unused,
  running,
  ready,
};

struct IntArgs
{
  uint64_t rdi = 0;
  uint64_t rsi = 0;
  uint64_t rdx = 0;
  uint64_t rcx = 0;
  uint64_t r8 = 0;
  uint64_t r9 = 0;

  uint64_t argcnt = 0;
};

struct FPArgs
{
  double xmm0 = 0;
  double xmm1 = 0;
  double xmm2 = 0;
  double xmm3 = 0;
  double xmm4 = 0;
  double xmm5 = 0;
  double xmm6 = 0;
  double xmm7 = 0;

  uint64_t argcnt = 0;
};

struct LngDblArgs : std::stack<long double>
{};

struct ArgStack : std::stack<uint8_t>
{};

struct gthread
{
  Context ctx;
  IntArgs iargs;
  FPArgs fargs;
  State state;
};

static gthread tbl[max_gthreads];
static gthread* cur_thrd;

void
gtinit(void);

[[noreturn]] void
gtret(int ret);

void
gtswtch(Context* oldctx, Context* newctx);

bool
gtyield();

void
gtstop(void);

#ifdef ICE_GREEN_IMPL_H

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iostream>
#include <tuple>
#include <type_traits>

IntArgs iargs;
FPArgs fargs;
LngDblArgs ldargstack;
ArgStack iargstack;
ArgStack fargstack;
int int_cnt = 0;
int fp_cnt = 0;

template<typename T,
         std::size_t I = 0U,
         std::size_t IntCnt = 0U,
         std::size_t FPCnt = 0U>
struct TupleIterator
{
  template<typename C>
  void operator()(T& objects, C callback)
  {
    if constexpr (I < std::tuple_size_v<T>) {
      callback(std::get<I>(objects), IntCnt, FPCnt);
      if constexpr (std::same_as<long double, decltype(std::get<I>(objects))>) {
        TupleIterator<T, I + 1U, IntCnt, FPCnt>{}(objects, callback);
        return;
      } else if constexpr (std::is_integral_v<decltype(std::get<I>(objects))>) {
        TupleIterator<T, I + 1U, IntCnt + 1U, FPCnt>{}(objects, callback);
        return;
      } else if constexpr (std::is_floating_point_v<decltype(std::get<I>(
                             objects))>) {
        TupleIterator<T, I + 1U, IntCnt, FPCnt + 1>{}(objects, callback);
        return;
      } else
        TupleIterator<T, I + 1U>{}(objects, callback);
    }
  }
};

constexpr void
arg_sorter_helper(auto elem, std::size_t intcnt, std::size_t fpcnt)
{
  auto assign = [&elem](auto& l) { l = elem; };

  if constexpr (std::same_as<long double, decltype(elem)>) {
    ldargstack.push(elem);
    return;
  }

  if constexpr (std::is_integral_v<decltype(elem)> && intcnt >= 6) {
    // int_cnt++;
    iargstack.push(elem);
    return;
  }

  if constexpr (std::is_floating_point_v<decltype(elem)> && fpcnt >= 8) {
    // fp_cnt++;
    fargstack.push(elem);
    return;
  }

  if constexpr (std::is_integral_v<decltype(elem)>) {
    // int_cnt++;
    switch (intcnt) {
      case 1:
        assign(iargs.rdi);
        break;
      case 2:
        assign(iargs.rsi);
        break;
      case 3:
        assign(iargs.rdx);
        break;
      case 4:
        assign(iargs.rcx);
        break;
      case 5:
        assign(iargs.r8);
        break;
      case 6:
        assign(iargs.r9);
        break;
      default:
        __builtin_unreachable();
    }
    return;
  } // if constexpr integral

  if constexpr (std::is_floating_point_v<decltype(elem)>) {
    // fp_cnt++;
    switch (fpcnt) {
      case 1:
        assign(fargs.xmm0);
        break;
      case 2:
        assign(fargs.xmm1);
        break;
      case 3:
        assign(fargs.xmm2);
        break;
      case 4:
        assign(fargs.xmm3);
        break;
      case 5:
        assign(fargs.xmm4);
        break;
      case 6:
        assign(fargs.xmm5);
        break;
      case 7:
        assign(fargs.xmm6);
        break;
      case 8:
        assign(fargs.xmm7);
        break;
      default:
        __builtin_unreachable();
    }
    return;
  } // if constexpr floating

  iargs.argcnt = intcnt;
  fargs.argcnt = fpcnt;
}

template<typename... Types>
void constexpr arg_sorter(Types... args)
{
  std::tuple<Types...> args_tuple{ args... };
  TupleIterator<decltype(args_tuple)> ti;
  ti(args_tuple, [](auto elem) { arg_sorter_helper(elem); });
}

void
gtswtch(Context* oldctx, Context* newctx, IntArgs& args)
{
  (void)oldctx;
  (void)newctx;

  register uint64_t reg_r8 asm("r8") = args.r8;
  register uint64_t reg_r9 asm("r9") = args.r9;
  register uint64_t reg_r10 asm("r10") = args.rdi;
  register uint64_t reg_r11 asm("r11") = args.rsi;

  asm("mov     %%rbp, %%rax\n" // preserve base pointer <-- this line needs
                               // checking in calling convention

      // ignore this line above

      "mov     %%rsp, 0x00(%%rdi)\n" // context switching
      "mov     %%r15, 0x08(%%rdi)\n"
      "mov     %%r14, 0x10(%%rdi)\n"
      "mov     %%r13, 0x18(%%rdi)\n"
      "mov     %%r12, 0x20(%%rdi)\n"
      "mov     %%rbx, 0x28(%%rdi)\n"
      "mov     %%rbp, 0x30(%%rdi)\n"

      "mov     0x00(%%rsi), %%rsp\n"
      "mov     0x08(%%rsi), %%r15\n"
      "mov     0x10(%%rsi), %%r14\n"
      "mov     0x18(%%rsi), %%r13\n"
      "mov     0x20(%%rsi), %%r12\n"
      "mov     0x28(%%rsi), %%rbx\n"
      "mov     0x30(%%rsi), %%rbp\n"

      // move from old stack (now %rax) to new stack (now %rbp)
      // above is an unverified choice of words, must confirm in calling
      // convention

      // ignore comment above

      "mov %%r10, %%rdi\n" // argument passing
      "mov %%r11, %%rsi\n"

      :
      : [reg_rdx] "d"(args.rdx),
        [reg_rcx] "c"(args.rcx),
        "r"(reg_r8),
        "r"(reg_r9),
        "r"(reg_r10),
        "r"(reg_r11)
      : "memory", "rdi", "rsi");
}

void
gtinit()
{
  cur_thrd = &tbl[0];
  cur_thrd->state = State::running;
}

[[noreturn]] void
gtret(int ret)
{
  if (cur_thrd != &tbl[0]) {
    cur_thrd->state = State::unused;
    gtyield();
    assert(!"reachable");
  }

  while (gtyield())
    ;

  exit(ret);
}

bool
gtyield()
{
  gthread* thrd;
  Context *oldctx, *newctx;

  thrd = cur_thrd;
  while (thrd->state != State::ready) {
    if (++thrd == &tbl[max_gthreads])
      thrd = &tbl[0];

    if (thrd == cur_thrd)
      return false;
  }

  if (cur_thrd->state != State::unused)
    cur_thrd->state = State::ready;

  thrd->state = State::running;
  oldctx = &cur_thrd->ctx;
  newctx = &thrd->ctx;

  cur_thrd = thrd;
  gtswtch(oldctx, newctx, thrd->iargs);
  return true;
}

static void
gtstop()
{
  gtret(0);
}

template<typename F, typename... Types>
int
gtgo(F& f, Types... args)
{
  gthread* p = nullptr;
  for (auto& thrd : tbl) {
    p = &thrd;
    if (p == &tbl[max_gthreads])
      return -1;
    if (p->state == State::unused)
      break;
  }

  std::uint8_t* stack = new std::uint8_t[stack_size];
  if (stack == nullptr)
    return -1;

  *(uint64_t*)&stack[stack_size - (1 * sizeof(uint64_t))] = (uint64_t)gtstop;
  *(uint64_t*)&stack[stack_size - (2 * sizeof(uint64_t))] = (uint64_t)f;
  p->ctx.rsp = (uint64_t)&stack[stack_size - (2 * sizeof(uint64_t))];
  p->state = State::ready;
  p->iargs = IntArgs{ *reinterpret_cast<uint64_t*>(&args)... };

  gtyield();
  return 0;
}

void
f(int j, int k, std::function<void(int, int, int)>* call)
{
  static int x;
  int id = ++x;

  (*call)(id, j, k);

  for (int i = 0; i < 10; i++) {
    std::cout << "Thread-" << id << ": " << i << '\n';
    gtyield();
  }
}

void
callback(int id, int i, int j)
{
  std::cout << "Callback from Thread-" << id << ": Args: " << i << ", " << j
            << '\n';
}

// int
// main(void)
// {
//   gtinit();
//   std::function<void(int, int, int)> function = callback;
//   gtgo(f, 5, 6, &function);
//   gtgo(f, 12, 13, &function);
//   gtret(1);
// }
#endif

#endif