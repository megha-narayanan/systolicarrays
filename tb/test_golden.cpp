// Tests for tb/golden_model.h. Run with: make test_golden
#include <cstdint>
#include <cstdio>
#include <vector>

#include "golden_model.h"

using namespace golden;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool ok, const char* what) {
  g_checks++;
  if (!ok) {
    g_failures++;
    std::printf("FAIL: %s\n", what);
  }
}

static void check_equal(const MatI32& expected, const MatI32& actual, int rows, int cols,
                        const char* what) {
  check(compare(expected, actual, rows, cols) == 0, what);
}

static MatI8 filled(int rows, int cols, int8_t v) {
  return MatI8(static_cast<std::size_t>(rows) * cols, v);
}

// 1. Hand-checked tiny cases
static void test_hand_checked() {
  // [1 2] [5 6]   [1*5+2*7  1*6+2*8]   [19 22]
  // [3 4] [7 8] = [3*5+4*7  3*6+4*8] = [43 50]
  {
    MatI8 A = {1, 2, 3, 4}, B = {5, 6, 7, 8};
    MatI32 want = {19, 22, 43, 50};
    check_equal(want, gemm_ref(A, B, 2, 2, 2), 2, 2, "2x2 positive (gemm_ref)");
    check_equal(want, gemm_tiled(A, B, 2, 2, 2, 8), 2, 2, "2x2 positive (gemm_tiled T=8)");
  }
  // [-1  2] [ 5 -6]   [-5-14   6+16]   [-19  22]
  // [ 3 -4] [-7  8] = [15+28 -18-32] = [ 43 -50]
  {
    MatI8 A = {-1, 2, 3, -4}, B = {5, -6, -7, 8};
    MatI32 want = {-19, 22, 43, -50};
    check_equal(want, gemm_ref(A, B, 2, 2, 2), 2, 2, "2x2 mixed signs");
  }
  // Non-square, catches swapped M/K/N:
  // [1 0 -2] [ 1  2]   [1*1+0*3+(-2)*5   1*2+0*4+(-2)*6]   [ -9 -10]
  // [4 1  3] [ 3  4] = [4*1+1*3+3*5      4*2+1*4+3*6   ] = [ 22  30]
  //          [ 5  6]
  {
    MatI8 A = {1, 0, -2, 4, 1, 3}, B = {1, 2, 3, 4, 5, 6};
    MatI32 want = {-9, -10, 22, 30};
    check_equal(want, gemm_ref(A, B, 2, 3, 2), 2, 2, "2x3x2 non-square");
  }
  {
    check(gemm_ref({-128}, {-128}, 1, 1, 1)[0] == 16384, "(-128)*(-128) == 16384");
    check(gemm_ref({127}, {-128}, 1, 1, 1)[0] == -16256, "127*(-128) == -16256");
    check(gemm_ref({127}, {127}, 1, 1, 1)[0] == 16129, "127*127 == 16129");
  }
  // C = I * B = B, with B[k][j] = 10k + j. Tile (1, 0) at T=2 is rows 2..3, cols 0..1.
  {
    MatI8 I4 = filled(4, 4, 0);
    for (int d = 0; d < 4; d++) I4[d * 4 + d] = 1;
    MatI8 B(16);
    for (int k = 0; k < 4; k++)
      for (int j = 0; j < 4; j++) B[k * 4 + j] = static_cast<int8_t>(10 * k + j);
    check_equal({20, 21, 30, 31}, tile_ref(I4, B, 4, 4, 4, 1, 0, 2), 2, 2,
                "tile_ref picks tile (1,0)");
  }
}

// 2. gemm_tiled == gemm_ref for T in {8, 16, 32}
static void test_tiled_matches_ref() {
  struct Shape { int M, K, N; const char* name; };
  const Shape shapes[] = {
      {1, 512, 512, "M=1 decode"},
      {1, 4096, 40, "M=1 decode, long K, N not a multiple of T"},
      {40, 1, 40, "K=1"},
      {1, 1, 1, "1x1x1"},
      {3, 5, 7, "smaller than one tile"},
      {37, 100, 53, "37x100x53 (no dim a multiple of T)"},
      {32, 32, 32, "exact multiple of every T"},
      {33, 17, 31, "one past / one short of a tile"},
      {64, 512, 512, "medium 64x512x512"},
  };
  const int Ts[] = {8, 16, 32};

  uint32_t seed = 418;
  for (const Shape& s : shapes) {
    const MatI8 A = random_int8_matrix(s.M, s.K, seed++);
    const MatI8 B = random_int8_matrix(s.K, s.N, seed++);
    const MatI32 ref = gemm_ref(A, B, s.M, s.K, s.N);
    for (int T : Ts) {
      char what[160];
      std::snprintf(what, sizeof what, "gemm_tiled == gemm_ref, %s (%dx%dx%d), T=%d", s.name,
                    s.M, s.K, s.N, T);
      check_equal(ref, gemm_tiled(A, B, s.M, s.K, s.N, T), s.M, s.N, what);
    }
  }
}

// 3. Edge tiles are zero-padded
static void test_edge_padding() {
  // Bottom-right tile (2, 3) at T=16: only rows 32..36 and cols 48..52 are real.
  const int M = 37, K = 100, N = 53, T = 16;
  const MatI8 A = random_int8_matrix(M, K, 1);
  const MatI8 B = random_int8_matrix(K, N, 2);
  const MatI32 ref = gemm_ref(A, B, M, K, N);
  const MatI32 tile = tile_ref(A, B, M, K, N, 2, 3, T);

  bool real_ok = true, pad_ok = true;
  for (int r = 0; r < T; r++) {
    for (int c = 0; c < T; c++) {
      const int i = 2 * T + r, j = 3 * T + c;
      const int32_t got = tile[r * T + c];
      if (i < M && j < N) real_ok &= (got == ref[i * N + j]);
      else                pad_ok &= (got == 0);
    }
  }
  check(real_ok, "edge tile: real entries match gemm_ref");
  check(pad_ok, "edge tile: padded entries are 0");

  const MatI8 At = a_tile(A, M, K, 2, T);
  const MatI8 Bt = b_tile(B, K, N, 3, T);
  bool at_ok = true, bt_ok = true;
  for (int r = 0; r < T; r++)
    for (int k = 0; k < K; k++)
      at_ok &= (At[r * K + k] == (2 * T + r < M ? A[(2 * T + r) * K + k] : 0));
  for (int k = 0; k < K; k++)
    for (int c = 0; c < T; c++)
      bt_ok &= (Bt[k * T + c] == (3 * T + c < N ? B[k * N + 3 * T + c] : 0));
  check(at_ok, "a_tile copies real rows and zero-pads the rest");
  check(bt_ok, "b_tile copies real cols and zero-pads the rest");
}

// 4. Edge values with closed-form answers
static void test_edge_values() {
  const int M = 37, K = 100, N = 53;
  const int Ts[] = {8, 16, 32};

  struct Case { int8_t a, b; int32_t per_mac; const char* name; };
  const Case cases[] = {
      {-128, -128, 16384, "all -128"},
      {127, 127, 16129, "all 127"},
      {-128, 127, -16256, "A=-128, B=127 (mixed signs)"},
      {127, -128, -16256, "A=127, B=-128 (mixed signs)"},
      {0, 0, 0, "all zeros"},
      {0, -128, 0, "A zero, B -128"},
  };
  for (const Case& cs : cases) {
    const MatI8 A = filled(M, K, cs.a), B = filled(K, N, cs.b);
    const MatI32 want(static_cast<std::size_t>(M) * N, cs.per_mac * K);
    char what[160];
    std::snprintf(what, sizeof what, "%s: every C(i,j) == %d * K (gemm_ref)", cs.name, cs.per_mac);
    check_equal(want, gemm_ref(A, B, M, K, N), M, N, what);
    for (int T : Ts) {
      std::snprintf(what, sizeof what, "%s (gemm_tiled T=%d)", cs.name, T);
      check_equal(want, gemm_tiled(A, B, M, K, N, T), M, N, what);
    }
  }

  // A rows alternate +1/-1 along K (K even), B all 127: sums cancel to 0.
  {
    MatI8 A(static_cast<std::size_t>(M) * K);
    for (int i = 0; i < M; i++)
      for (int k = 0; k < K; k++) A[i * K + k] = (k % 2 == 0) ? 1 : -1;
    const MatI32 zero(static_cast<std::size_t>(M) * N, 0);
    check_equal(zero, gemm_ref(A, filled(K, N, 127), M, K, N), M, N,
                "alternating +1/-1 times 127 cancels to 0");
  }

  {
    const int S = 45;  // not a multiple of any T
    MatI8 I = filled(S, S, 0);
    for (int d = 0; d < S; d++) I[d * S + d] = 1;
    const MatI8 A = random_int8_matrix(M, S, 7);
    const MatI8 B = random_int8_matrix(S, N, 8);
    const MatI32 A32(A.begin(), A.end()), B32(B.begin(), B.end());
    check_equal(A32, gemm_ref(A, I, M, S, S), M, S, "A * I == A");
    check_equal(B32, gemm_ref(I, B, S, S, N), S, N, "I * B == B");
    for (int T : Ts) {
      char what[64];
      std::snprintf(what, sizeof what, "A * I == A (gemm_tiled T=%d)", T);
      check_equal(A32, gemm_tiled(A, I, M, S, S, T), M, S, what);
    }
  }
}

// 5. INT32 overflow bound (see golden_model.h)
static void test_overflow_bound() {
  {
    const int K = 14336;
    const int32_t got = gemm_ref(filled(1, K, -128), filled(K, 1, -128), 1, K, 1)[0];
    check(got == 16384 * 14336, "K=14336 all -128: 234,881,024, no overflow");
  }
  {
    const int K = 131071;
    const int32_t got = gemm_ref(filled(1, K, -128), filled(K, 1, -128), 1, K, 1)[0];
    check(got == 2147467264, "K=131071 all -128: 2,147,467,264, fits in INT32");
  }
  {
    const int K = 131072;
    const int32_t got = gemm_ref(filled(1, K, -128), filled(K, 1, -128), 1, K, 1)[0];
    check(got == INT32_MIN, "K=131072 all -128: wraps to INT32_MIN (32-bit wraparound)");
  }
}

// 6. INT4 pack/unpack
static void test_int4() {
  std::vector<int8_t> all16;
  for (int v = -8; v <= 7; v++) all16.push_back(static_cast<int8_t>(v));
  const std::vector<uint8_t> packed = pack_int4(all16);
  check(packed.size() == 8, "16 INT4 values pack into 8 bytes");
  check(unpack_int4(packed, all16.size()) == all16, "INT4 round-trip, all 16 values");

  bool each_ok = true;
  for (int lo = -8; lo <= 7; lo++) {
    for (int hi = -8; hi <= 7; hi++) {
      const std::vector<int8_t> pair = {static_cast<int8_t>(lo), static_cast<int8_t>(hi)};
      each_ok &= (unpack_int4(pack_int4(pair), 2) == pair);
    }
  }
  check(each_ok, "INT4 round-trip, all 256 (low, high) pairs");

  check(pack_int4({1, -1}) == std::vector<uint8_t>{0xF1}, "pack {1, -1} == 0xF1 (low nibble first)");
  check(pack_int4({-8, 7}) == std::vector<uint8_t>{0x78}, "pack {-8, 7} == 0x78");
  check(pack_int4({0, 0}) == std::vector<uint8_t>{0x00}, "pack {0, 0} == 0x00");

  bool bytes_ok = true;
  for (int b = 0; b < 256; b++) {
    const std::vector<uint8_t> one = {static_cast<uint8_t>(b)};
    bytes_ok &= (pack_int4(unpack_int4(one, 2)) == one);
  }
  check(bytes_ok, "every byte 0x00..0xFF unpacks and repacks to itself");

  check(sign_extend_int4(0x7) == 7 && sign_extend_int4(0x8) == -8 && sign_extend_int4(0xF) == -1 &&
            sign_extend_int4(0x0) == 0,
        "sign_extend_int4: 0x7->7, 0x8->-8, 0xF->-1, 0x0->0");

  check(pack_int4({3, -2, 5}) == std::vector<uint8_t>{0xE3, 0x05}, "odd count pads high nibble with 0");
  check(unpack_int4(pack_int4({3, -2, 5}), 3) == std::vector<int8_t>{3, -2, 5}, "odd count round-trip");

  bool threw = false;
  try { pack_int4({8}); } catch (const std::invalid_argument&) { threw = true; }
  check(threw, "pack_int4 rejects 8 (outside -8..7)");
}

// 7. compare() must catch mismatches, or every test above would pass vacuously.
static void test_compare() {
  const MatI32 a = {1, 2, 3, 4, 5, 6};
  MatI32 b = a;
  check(compare(a, b, 2, 3) == 0, "compare: identical matrices -> 0 mismatches");

  std::printf("  (expected output from the next check:)\n");
  b[4] = -5;  // (i=1, j=1)
  b[5] = 0;   // (i=1, j=2)
  check(compare(a, b, 2, 3) == 2, "compare: two changed entries -> 2 mismatches");

  bool threw = false;
  try { compare(a, b, 3, 3); } catch (const std::invalid_argument&) { threw = true; }
  check(threw, "compare rejects mismatched dimensions");

  check(random_int8_matrix(16, 16, 5) == random_int8_matrix(16, 16, 5), "random_int8_matrix is reproducible");
  check(random_int8_matrix(16, 16, 5) != random_int8_matrix(16, 16, 6), "different seeds differ");
}

int main() {
  struct Section { const char* name; void (*fn)(); };
  const Section sections[] = {
      {"hand-checked tiny cases", test_hand_checked},
      {"gemm_tiled == gemm_ref (T = 8, 16, 32)", test_tiled_matches_ref},
      {"edge tiles zero-padded", test_edge_padding},
      {"edge values", test_edge_values},
      {"INT32 overflow bound", test_overflow_bound},
      {"INT4 pack/unpack", test_int4},
      {"compare / random helpers", test_compare},
  };
  for (const Section& s : sections) {
    const int checks_before = g_checks, failures_before = g_failures;
    s.fn();
    std::printf("[%s] %s: %d checks\n", g_failures == failures_before ? "PASS" : "FAIL", s.name,
                g_checks - checks_before);
  }

  if (g_failures) {
    std::printf("test_golden: FAILED (%d of %d checks)\n", g_failures, g_checks);
    return 1;
  }
  std::printf("test_golden: PASS (%d checks)\n", g_checks);
  return 0;
}
