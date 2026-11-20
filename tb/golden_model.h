// Golden reference model for the INT8 output-stationary systolic array.
// Checks values only, not timing. Header-only C++17, no Verilator dependency.
//
// Data contract (rtl/pe.sv, docs/interface.md):
//  * Signed INT8 operands, signed INT32 accumulators.
//  * Row-major: A is MxK, B is KxN, C is MxN.
//  * One hardware run computes one T x T tile of C over the full K, starting
//    from zero. Edge tiles are zero-padded.
//  * INT4 weights: two signed 4-bit values per byte, low nibble first,
//    sign-extended to INT8 before entering the array.
//
// Why INT32 can't overflow for our shapes:
// max |product| = (-128)*(-128) = 16384 = 2^14, so |sum| <= 16384 * K.
// For K <= 14336 (our largest K) that's <= 234,881,024 (~2^27.8) < 2^31.
// The exact limit is K = 131,071; at K = 131,072 all -128 gives 2^31 and wraps.
// The model wraps at 32 bits like the hardware (the add is done in uint32_t,
// since signed overflow is undefined in C++).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace golden {

using MatI8  = std::vector<int8_t>;   // row-major
using MatI32 = std::vector<int32_t>;  // row-major

// Throws if a buffer's size doesn't match rows x cols.
inline void check_size(std::size_t actual, int rows, int cols, const char* name) {
  if (rows < 0 || cols < 0)
    throw std::invalid_argument(std::string(name) + ": negative dimension");
  if (actual != static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols))
    throw std::invalid_argument(std::string(name) + ": buffer has " + std::to_string(actual) +
                                " elements, expected " + std::to_string(rows) + "x" +
                                std::to_string(cols));
}

// acc + a*b, widened to int32 before multiplying, wrapping at 32 bits.
inline int32_t mac(int32_t acc, int8_t a, int8_t b) {
  const int32_t product = static_cast<int32_t>(a) * static_cast<int32_t>(b);
  const uint32_t sum = static_cast<uint32_t>(acc) + static_cast<uint32_t>(product);
  return static_cast<int32_t>(sum);
}

inline int num_tiles(int n, int T) { return (n + T - 1) / T; }

// Plain triple-loop C = A * B. The reference for everything else.
inline MatI32 gemm_ref(const MatI8& A, const MatI8& B, int M, int K, int N) {
  check_size(A.size(), M, K, "gemm_ref A");
  check_size(B.size(), K, N, "gemm_ref B");

  MatI32 C(static_cast<std::size_t>(M) * N, 0);
  for (int i = 0; i < M; i++) {
    for (int j = 0; j < N; j++) {
      int32_t acc = 0;
      for (int k = 0; k < K; k++)
        acc = mac(acc, A[static_cast<std::size_t>(i) * K + k], B[static_cast<std::size_t>(k) * N + j]);
      C[static_cast<std::size_t>(i) * N + j] = acc;
    }
  }
  return C;
}

// T_rows x K slice of A for tile row `tile_row`; rows past M are zero.
inline MatI8 a_tile(const MatI8& A, int M, int K, int tile_row, int T_rows) {
  check_size(A.size(), M, K, "a_tile A");
  MatI8 tile(static_cast<std::size_t>(T_rows) * K, 0);
  for (int r = 0; r < T_rows; r++) {
    const int i = tile_row * T_rows + r;
    if (i >= M) break;
    for (int k = 0; k < K; k++)
      tile[static_cast<std::size_t>(r) * K + k] = A[static_cast<std::size_t>(i) * K + k];
  }
  return tile;
}

// K x T_cols slice of B for tile column `tile_col`; columns past N are zero.
inline MatI8 b_tile(const MatI8& B, int K, int N, int tile_col, int T_cols) {
  check_size(B.size(), K, N, "b_tile B");
  MatI8 tile(static_cast<std::size_t>(K) * T_cols, 0);
  for (int k = 0; k < K; k++) {
    for (int c = 0; c < T_cols; c++) {
      const int j = tile_col * T_cols + c;
      if (j >= N) break;
      tile[static_cast<std::size_t>(k) * T_cols + c] = B[static_cast<std::size_t>(k) * N + j];
    }
  }
  return tile;
}

// One hardware run: tile (tile_row, tile_col) of C, zero-padded at the edges.
// tile_row/tile_col are tile indices, not element offsets. The rectangular
// overload matches the RTL's separate ROWS/COLS parameters.
inline MatI32 tile_ref(const MatI8& A, const MatI8& B, int M, int K, int N,
                       int tile_row, int tile_col, int T_rows, int T_cols) {
  const MatI8 At = a_tile(A, M, K, tile_row, T_rows);
  const MatI8 Bt = b_tile(B, K, N, tile_col, T_cols);
  return gemm_ref(At, Bt, T_rows, K, T_cols);
}

inline MatI32 tile_ref(const MatI8& A, const MatI8& B, int M, int K, int N,
                       int tile_row, int tile_col, int T) {
  return tile_ref(A, B, M, K, N, tile_row, tile_col, T, T);
}

// Full C built from tile_ref results, dropping the edge padding.
inline MatI32 gemm_tiled(const MatI8& A, const MatI8& B, int M, int K, int N, int T) {
  if (T <= 0) throw std::invalid_argument("gemm_tiled: T must be positive");
  check_size(A.size(), M, K, "gemm_tiled A");
  check_size(B.size(), K, N, "gemm_tiled B");

  MatI32 C(static_cast<std::size_t>(M) * N, 0);
  for (int tr = 0; tr < num_tiles(M, T); tr++) {
    for (int tc = 0; tc < num_tiles(N, T); tc++) {
      const MatI32 tile = tile_ref(A, B, M, K, N, tr, tc, T);
      for (int r = 0; r < T; r++) {
        const int i = tr * T + r;
        if (i >= M) break;
        for (int c = 0; c < T; c++) {
          const int j = tc * T + c;
          if (j >= N) break;
          C[static_cast<std::size_t>(i) * N + j] = tile[static_cast<std::size_t>(r) * T + c];
        }
      }
    }
  }
  return C;
}

// INT4: byte = (v[2n] & 0xF) | (v[2n+1] << 4), e.g. {1, -1} -> 0xF1.
// An odd count leaves the last high nibble 0. Values must be in -8..7.
inline std::vector<uint8_t> pack_int4(const std::vector<int8_t>& values) {
  std::vector<uint8_t> packed((values.size() + 1) / 2, 0);
  for (std::size_t n = 0; n < values.size(); n++) {
    const int8_t v = values[n];
    if (v < -8 || v > 7)
      throw std::invalid_argument("pack_int4: value " + std::to_string(v) + " at index " +
                                  std::to_string(n) + " is outside -8..7");
    const uint8_t nibble = static_cast<uint8_t>(v) & 0x0F;
    if (n % 2 == 0) packed[n / 2] |= nibble;
    else            packed[n / 2] |= static_cast<uint8_t>(nibble << 4);
  }
  return packed;
}

// 0x0..0x7 -> 0..7, 0x8..0xF -> -8..-1.
inline int8_t sign_extend_int4(uint8_t nibble) {
  return static_cast<int8_t>(static_cast<int>((nibble & 0x0F) ^ 0x08) - 8);
}

inline std::vector<int8_t> unpack_int4(const std::vector<uint8_t>& packed, std::size_t count) {
  if (count > packed.size() * 2)
    throw std::invalid_argument("unpack_int4: count " + std::to_string(count) +
                                " needs more than the " + std::to_string(packed.size()) +
                                " bytes given");
  std::vector<int8_t> values(count);
  for (std::size_t n = 0; n < count; n++) {
    const uint8_t byte = packed[n / 2];
    const uint8_t nibble = (n % 2 == 0) ? (byte & 0x0F) : (byte >> 4);
    values[n] = sign_extend_int4(nibble);
  }
  return values;
}

// Returns the mismatch count; prints the first mismatch as (i, j, expected, actual).
inline std::size_t compare(const MatI32& expected, const MatI32& actual, int rows, int cols) {
  check_size(expected.size(), rows, cols, "compare expected");
  check_size(actual.size(), rows, cols, "compare actual");

  std::size_t mismatches = 0;
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
      const std::size_t idx = static_cast<std::size_t>(i) * cols + j;
      if (expected[idx] != actual[idx]) {
        if (mismatches == 0)
          std::printf("  first mismatch at (i=%d, j=%d): expected %ld, actual %ld\n", i, j,
                      static_cast<long>(expected[idx]), static_cast<long>(actual[idx]));
        mismatches++;
      }
    }
  }
  if (mismatches > 0)
    std::printf("  %zu of %zu entries differ\n", mismatches,
                static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));
  return mismatches;
}

// Uses raw mt19937 output, not uniform_int_distribution, because only the
// former is identical across libstdc++, libc++ and MSVC.
inline MatI8 random_int8_matrix(int rows, int cols, uint32_t seed) {
  if (rows < 0 || cols < 0) throw std::invalid_argument("random_int8_matrix: negative dimension");
  std::mt19937 rng(seed);
  MatI8 m(static_cast<std::size_t>(rows) * cols);
  for (auto& x : m) x = static_cast<int8_t>(static_cast<int>(rng() >> 24) - 128);
  return m;
}

}  // namespace golden
