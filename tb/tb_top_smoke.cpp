// Harness skeleton for systolic_top. Today it only proves the interface builds
// and can be driven from C++. Megha extends this into the real harness (task 1.4).
#include <cstdint>
#include <cstdio>
#include <memory>
#include "Vsystolic_top.h"
#include "verilated.h"

#ifndef ROWS
#define ROWS 8
#endif
#ifndef COLS
#define COLS 8
#endif

static void tick(Vsystolic_top* t) { t->clk = 0; t->eval(); t->clk = 1; t->eval(); }

int main(int argc, char** argv) {
  auto ctx = std::make_unique<VerilatedContext>();
  ctx->commandArgs(argc, argv);
  auto t = std::make_unique<Vsystolic_top>(ctx.get());

  // Reset.
  t->rst = 1; t->enable = 1; t->in_valid = 0; t->in_last = 0;
  for (int c = 0; c < 4; c++) tick(t.get());
  t->rst = 0;

  // Stream one K=4 tile of k-slices. Unpacked SV array ports appear as C arrays;
  // signed INT8 values are written as their raw byte.
  const int K = 4;
  for (int k = 0; k < K; k++) {
    for (int i = 0; i < ROWS; i++) t->a_in[i] = (uint8_t)(int8_t)(i - k);
    for (int j = 0; j < COLS; j++) t->b_in[j] = (uint8_t)(int8_t)(j + k);
    t->in_valid = 1; t->in_last = (k == K - 1);
    tick(t.get());
  }
  t->in_valid = 0; t->in_last = 0;

  // Wait for done (the stub never asserts it, so this just times out cleanly).
  int cycles = 0;
  while (!t->done && cycles < 4 * (ROWS + COLS + K)) { tick(t.get()); cycles++; }

  t->final();
  std::printf("tb_top_smoke: PASS (interface builds and runs; done=%d after %d cycles%s)\n",
              (int)t->done, cycles, t->done ? "" : ", expected 0 from the stub");
  return 0;
}
