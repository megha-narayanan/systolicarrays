# Out-of-context synthesis + place-and-route for one configuration.
# Usage (from repo root):
#   vivado -mode batch -nojournal -nolog -source scripts/synth_ooc.tcl \
#          -tclargs <top> <period_ns> [ROWS COLS]
# Example: ... -tclargs pe 2.0
#          ... -tclargs systolic_top 3.0 16 16
# Target part: Kria K26 (KV260). VERIFY this string in Vivado: get_parts xck26*
set part   "xck26-sfvc784-2LV-c"
set top    [lindex $argv 0]
set period [lindex $argv 1]
set gen    {}
set tag    $top
if {[llength $argv] >= 4} {
  set gen [list -generic ROWS=[lindex $argv 2] -generic COLS=[lindex $argv 3]]
  set tag "${top}_[lindex $argv 2]x[lindex $argv 3]"
}
file mkdir build/vivado

read_verilog -sv [glob rtl/*.sv]
synth_design -top $top -part $part -mode out_of_context {*}$gen
create_clock -name clk -period $period [get_ports clk]
opt_design
place_design
route_design

report_utilization     -file build/vivado/${tag}_util.rpt
report_timing_summary  -file build/vivado/${tag}_timing.rpt
set wns  [get_property SLACK [get_timing_paths -max_paths 1 -setup]]
set fmax [expr {1000.0 / ($period - $wns)}]
puts [format "RESULT %s period=%.3fns WNS=%.3fns Fmax~%.1fMHz" $tag $period $wns $fmax]
