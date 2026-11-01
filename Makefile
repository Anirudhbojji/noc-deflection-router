VERILATOR ?= verilator
VFLAGS    := --cc --exe --build -O3 -Wall -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM
RTL       := rtl/noc_pkg.sv rtl/router.sv
RTL_MESH  := $(RTL) rtl/mesh.sv
RTL_NI    := rtl/noc_pkg.sv rtl/ni.sv

.PHONY: all test test_router test_ni test_mesh plots clean

all: test

test: test_router test_ni test_mesh

build/router/Vrouter: $(RTL) tb/tb_router.cpp tb/noc_tb.h
	mkdir -p build/router
	$(VERILATOR) $(VFLAGS) --Mdir build/router --top-module router \
		-CFLAGS -I$(abspath tb) \
		-GMY_X=1 -GMY_Y=1 $(RTL) $(abspath tb/tb_router.cpp) -o Vrouter

build/ni/Vni: $(RTL_NI) tb/tb_ni.cpp tb/noc_tb.h
	mkdir -p build/ni
	$(VERILATOR) $(VFLAGS) --Mdir build/ni --top-module ni \
		-CFLAGS -I$(abspath tb) \
		$(RTL_NI) $(abspath tb/tb_ni.cpp) -o Vni

build/mesh/Vmesh: $(RTL_MESH) tb/tb_mesh.cpp tb/noc_tb.h
	mkdir -p build/mesh
	$(VERILATOR) $(VFLAGS) --Mdir build/mesh --top-module mesh \
		-CFLAGS -I$(abspath tb) \
		$(RTL_MESH) $(abspath tb/tb_mesh.cpp) -o Vmesh

test_router: build/router/Vrouter
	./build/router/Vrouter

test_ni: build/ni/Vni
	./build/ni/Vni

test_mesh: build/mesh/Vmesh
	mkdir -p results
	./build/mesh/Vmesh results/sweep.csv

plots: results/sweep.csv
	python3 scripts/plot_results.py results/sweep.csv docs

clean:
	rm -rf build results
