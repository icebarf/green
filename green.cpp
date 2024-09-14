#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>

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

struct RegisterArgs
{
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t r8;
  uint64_t r9;
};

struct gthread
{
  Context ctx;
  RegisterArgs args;
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
static void
gtstop(void);

void
gtswtch(Context* oldctx, Context* newctx, RegisterArgs& args)
{
  (void)oldctx;
  (void)newctx;

  register uint64_t reg_r8 asm("r8") = args.r8;
  register uint64_t reg_r9 asm("r9") = args.r9;
  register uint64_t reg_r10 asm("r10") = args.rdi;
  register uint64_t reg_r11 asm("r11") = args.rsi;

  asm("mov     %%rbp, %%rax\n" // preserve base pointer <-- this line needs
                               // checking in calling convention

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
  gtswtch(oldctx, newctx, thrd->args);
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
  p->args = RegisterArgs{ *reinterpret_cast<uint64_t*>(&args)..., 0, 0, 0 };

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

int
main(void)
{
  gtinit();
  std::function<void(int, int, int)> function = callback;
  gtgo(f, 5, 6, &function);
  gtgo(f, 12, 13, &function);
  gtret(1);
}
