// Self-checking testbench for one PE: directed edge cases, then a long random
// run compared cycle-by-cycle against a C++ reference model.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include "Vpe.h"
#include "verilated.h"

static int failures = 0;

struct Ref {  // C++ model of the PE's registers
  int8_t a = 0, b = 0; bool valid = false; int32_t acc = 0;
  void step(bool en, bool clr, bool drn, int8_t ai, int8_t bi, bool vi, int32_t acc_in) {
    if (!en) return;
    int32_t next = acc;
    if (clr)       next = 0;
    else if (drn)  next = acc_in;
    else if (vi)   next = (int32_t)((uint32_t)acc + (uint32_t)((int32_t)ai * (int32_t)bi));  // wraps like hardware
    a = ai; b = bi; valid = vi; acc = next;
  }
};

static void tick(Vpe* p) { p->clk = 0; p->eval(); p->clk = 1; p->eval(); }

static void drive(Vpe* p, Ref& r, bool en, bool clr, bool drn, int8_t a, int8_t b, bool v, int32_t acc_in) {
  p->enable = en; p->clear = clr; p->drain = drn;
  p->a_in = (uint8_t)a; p->b_in = (uint8_t)b; p->valid_in = v; p->acc_in = (uint32_t)acc_in;
  tick(p);
  r.step(en, clr, drn, a, b, v, acc_in);
}

static void check(Vpe* p, const Ref& r, const char* what, long cycle) {
  int8_t a = (int8_t)p->a_out, b = (int8_t)p->b_out;
  int32_t acc = (int32_t)p->acc_out;
  if (a != r.a || b != r.b || (bool)p->valid_out != r.valid || acc != r.acc) {
    if (failures++ < 10)
      std::printf("FAIL %s @%ld: dut a=%d b=%d v=%d acc=%d | ref a=%d b=%d v=%d acc=%d\n", what, cycle,
                  a, b, (int)p->valid_out, acc, r.a, r.b, (int)r.valid, r.acc);
  }
}

int main(int argc, char** argv) {
  auto ctx = std::make_unique<VerilatedContext>();
  ctx->randReset(2);            // randomize uninitialized regs: exposes missing clears
  ctx->commandArgs(argc, argv);
  auto p = std::make_unique<Vpe>(ctx.get());
  Ref r;

  // Sync DUT and model: first enabled cycle clears acc and loads a/b/valid.
  drive(p.get(), r, true, true, false, 0, 0, false, 0);
  check(p.get(), r, "init", 0);

  // Directed: signed corner cases.
  drive(p.get(), r, true, false, false, -128, -128, true, 0);       // +16384
  check(p.get(), r, "-128*-128", 1);
  if ((int32_t)p->acc_out != 16384) { std::printf("FAIL -128*-128 gave %d\n", (int32_t)p->acc_out); failures++; }
  drive(p.get(), r, true, false, false, 127, -128, true, 0);        // 16384 - 16256 = 128
  check(p.get(), r, "127*-128", 2);
  drive(p.get(), r, false, false, false, 5, 5, true, 0);            // stall: nothing changes
  check(p.get(), r, "stall", 3);
  drive(p.get(), r, true, false, false, 9, 9, false, 0);            // valid=0: no MAC
  check(p.get(), r, "invalid", 4);
  drive(p.get(), r, true, false, true, 0, 0, false, 777);           // drain: take acc_in
  check(p.get(), r, "drain", 5);
  drive(p.get(), r, true, true, false, 0, 0, false, 0);             // clear
  check(p.get(), r, "clear", 6);

  // Random: mostly MACs, with stalls, clears, drains, and bubbles mixed in.
  std::mt19937 rng(418);
  std::uniform_int_distribution<int> byte(-128, 127), pct(0, 99);
  std::uniform_int_distribution<int32_t> word(INT32_MIN, INT32_MAX);
  const long N = 200000;
  for (long c = 7; c < N; c++) {
    bool en = pct(rng) < 90, clr = pct(rng) < 3, drn = pct(rng) < 5, v = pct(rng) < 85;
    drive(p.get(), r, en, clr, drn, (int8_t)byte(rng), (int8_t)byte(rng), v, word(rng));
    check(p.get(), r, "random", c);
  }

  p->final();
  if (failures) { std::printf("tb_pe: FAILED (%d mismatches)\n", failures); return 1; }
  std::printf("tb_pe: PASS (%ld cycles)\n", N);
  return 0;
}
