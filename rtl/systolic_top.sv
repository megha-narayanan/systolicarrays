// Top-level interface for the systolic array. Ports are the contract with the
// C++ harness
/* verilator lint_off UNUSEDSIGNAL */  // STUB ONLY: delete once the body uses every input
module systolic_top #(
  parameter int ROWS   = 8,
  parameter int COLS   = 8,
  parameter int DATA_W = 8,    // INT8 operands
  parameter int ACC_W  = 32    // INT32 accumulators
) (
  input  logic                     clk,
  input  logic                     rst,

  //one k-slice per cycle, UNSKEWED. (skew is done inside).
  input  logic signed [DATA_W-1:0] a_in [ROWS],   // column k of the A tile
  input  logic signed [DATA_W-1:0] b_in [COLS],   // row k of the B tile
  input  logic                     in_valid,      // a_in/b_in hold a real k-slice
  input  logic                     in_last,       // final k-slice of this tile
  input  logic                     enable,        // 0 = freeze everything (stall)

  // Results: drained one row per cycle through the bottom edge.
  output logic signed [ACC_W-1:0]  c_out [COLS],
  output logic                     c_valid,
  output logic                     done           // tile fully drained
);
  // ---- STUB: replace in section 1 (skew chains, PE grid, drain chain, FSM) ----
  always_comb begin
    for (int j = 0; j < COLS; j++) c_out[j] = '0;
    c_valid = 1'b0;
    done    = 1'b0;
  end

`ifdef PERF_COUNTERS
  // Section 2: busy / stall / fill / drain counters go here.
`endif
endmodule
