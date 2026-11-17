# Build and run everything:   make
# Lint only:                  make lint
# Pick array size:            make test_top ROWS=16 COLS=16
VERILATOR ?= verilator
ROWS ?= 8
COLS ?= 8
BUILD := $(CURDIR)/build
VFLAGS := -Wall --cc --exe --build -j 0 -O2 --x-assign unique --x-initial unique

.PHONY: all test lint test_pe test_top clean
all: lint test
test: test_pe test_top

lint:
	$(VERILATOR) --lint-only -Wall --top-module pe rtl/pe.sv
	$(VERILATOR) --lint-only -Wall --top-module systolic_top -GROWS=$(ROWS) -GCOLS=$(COLS) rtl/systolic_top.sv

test_pe:
	mkdir -p $(BUILD)/pe
	$(VERILATOR) $(VFLAGS) --top-module pe -Mdir $(BUILD)/pe $(CURDIR)/rtl/pe.sv $(CURDIR)/tb/tb_pe.cpp
	$(BUILD)/pe/Vpe

test_top:
	mkdir -p $(BUILD)/top_$(ROWS)x$(COLS)
	$(VERILATOR) $(VFLAGS) --top-module systolic_top -GROWS=$(ROWS) -GCOLS=$(COLS) \
	  -CFLAGS "-DROWS=$(ROWS) -DCOLS=$(COLS)" -Mdir $(BUILD)/top_$(ROWS)x$(COLS) \
	  $(CURDIR)/rtl/pe.sv $(CURDIR)/rtl/systolic_top.sv $(CURDIR)/tb/tb_top_smoke.cpp
	$(BUILD)/top_$(ROWS)x$(COLS)/Vsystolic_top

clean:
	rm -rf $(BUILD)
