// Processing element for an output-stationary INT8 systolic array.
// a flows left -> right, b flows top -> bottom, accumulation stays put (except during drain).
module pe #(
  parameter int DATA_W = 8,
  parameter int ACC_W  = 32
) (
  input  logic                     clk,
  input  logic                     enable,     // 0 = hold every register (stall)
  input  logic                     clear,      // zero the accumulator (start of tile)
  input  logic                     drain,      // shift accumulators down the column
  input  logic signed [DATA_W-1:0] a_in,       // from left neighbor
  input  logic signed [DATA_W-1:0] b_in,       // from top neighbor
  input  logic                     valid_in,   // a_in/b_in are real data this cycle
  input  logic signed [ACC_W-1:0]  acc_in,     // from PE above (used only in drain)
  output logic signed [DATA_W-1:0] a_out,      // to right neighbor
  output logic signed [DATA_W-1:0] b_out,      // to bottom neighbor
  output logic                     valid_out,
  output logic signed [ACC_W-1:0]  acc_out     // this PE's accumulator / to PE below
);
  always_ff @(posedge clk) begin
    if (enable) begin
      a_out <= a_in;
      b_out <= b_in;
      valid_out <= valid_in;
      if (clear) acc_out <= '0;
      else if (drain) acc_out <= acc_in;
      else if (valid_in) acc_out <= acc_out + a_in * b_in;  // all operands signed
    end
  end
endmodule
