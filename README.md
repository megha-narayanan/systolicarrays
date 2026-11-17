# Systolic Arrays for LLM Inference (15-418 final project)

Raj Methi and Megha Narayanan

## Quick start
Requires Verilator 5.x, a C++17 compiler, and make.
```
make          # lint + all tests
make lint     # Verilator -Wall lint only
make test_pe  # PE unit test (directed + 200k random cycles vs. C++ model)
make test_top ROWS=16 COLS=16   # top-level harness smoke test at any size
```

## Layout
| Path | What |
|---|---|
| rtl/pe.sv | Processing element: signed INT8 MAC into INT32, pass-through, drain |
| rtl/systolic_top.sv | Top-level ports (stub body until section 1) |
| tb/tb_pe.cpp | Self-checking PE testbench |
| tb/tb_top_smoke.cpp | Harness skeleton for systolic_top (grows into the real harness) |
| docs/interface.md | The RTL/harness contract. Change this before changing ports |
| scripts/synth_ooc.tcl | Vivado out-of-context synth + P&R, prints Fmax |

## Section 0 checklist
- [x] Repo builds and runs a test with one command
- [x] PE written and passing its unit test
- [ ] Interface spec agreed (docs/interface.md checkboxes)
- [ ] Verilator working on GHC machines (Megha)
- [ ] Vivado installed; K26 part string verified; `pe` synthesizes (Raj)
