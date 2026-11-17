# systolic_top interface (v0, draft for Raj + Megha to agree on)

The RTL and the C++ harness meet only at these ports. Change this file first, then the code.

## Parameters
| Name | Default | Meaning |
|---|---|---|
| ROWS | 8 | PE rows (= rows of the C tile, the M dimension) |
| COLS | 8 | PE columns (= columns of the C tile, the N dimension) |
| DATA_W | 8 | Operand width, signed (INT8) |
| ACC_W | 32 | Accumulator width, signed (INT32) |

## Ports
| Port | Dir | Width | Meaning |
|---|---|---|---|
| clk | in | 1 | Rising-edge clock |
| rst | in | 1 | Synchronous reset, active high |
| a_in[ROWS] | in | signed DATA_W each | Column k of the A tile, one k-slice per cycle, **unskewed** |
| b_in[COLS] | in | signed DATA_W each | Row k of the B tile, same cycle as a_in, **unskewed** |
| in_valid | in | 1 | a_in/b_in hold a real k-slice this cycle |
| in_last | in | 1 | This is the tile's final k-slice |
| enable | in | 1 | 0 = every register in the design holds (stall) |
| c_out[COLS] | out | signed ACC_W each | One row of C per cycle while c_valid is high |
| c_valid | out | 1 | c_out holds a real result row |
| done | out | 1 | High for one cycle when the whole tile has drained |

In Verilator's C++ model, the unpacked arrays are C arrays (`top->a_in[i]`). Write signed INT8 as its raw byte: `top->a_in[i] = (uint8_t)(int8_t)x;` and read results as `(int32_t)top->c_out[j]`.

## Agreed behavior (check each box when both agree)
- [ ] **Skew is inside the RTL.** The harness sends plain k-slices; row i / column j are delayed i / j cycles internally.
- [ ] **Tile boundaries.** The first in_valid after idle or done starts a tile and clears the accumulators. in_last marks the final slice. K is never sent as a number.
- [ ] **Stall.** With enable = 0, nothing changes anywhere, including skew chains and drain. Holding enable low mid-tile must not change the result.
- [ ] **Latency.** First c_valid comes a fixed number of cycles after in_last (about ROWS + COLS). Exact number: _TBD once the drain exists_; the harness asserts it.
- [ ] **Row order out.** Rows of C come out bottom row first (ROWS-1 down to 0), one per cycle, because the drain shifts down the columns. _Confirm when drain is written._
- [ ] **One tile at a time.** The harness waits for done before starting the next tile. (Overlap is a 125% goal.)
- [ ] **Overflow.** Accumulators wrap at 32 bits like the golden model. Not reachable for K up to 131,072 at INT8, so no saturation logic.

## Counters (compiled in with -DPERF_COUNTERS, section 2)
busy_cycles, stall_cycles, fill_cycles, drain_cycles: exposed as outputs or `/*verilator public*/` signals. Per-PE activity for the heatmap is sampled from C++, not counted in RTL.
