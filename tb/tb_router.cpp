// Unit tests for a single router (instantiated at mesh position (1,1) of a
// 4x4, so all five ports exist).
//
//   Test A  collision -> deflection: two flits want East in the same cycle;
//           exactly one gets East, the other leaves on a different port the
//           same cycle (deflected, not stalled, not dropped).
//   Test B  credit exhaustion: East's 2 credits are never returned; after two
//           sends every later East-bound flit deflects; with ALL direction
//           credits exhausted, flits stall in the FIFOs (zero output) and
//           nothing is lost; returning credits drains everything.
//   Test C  ejection: a flit addressed to (1,1) leaves on the Local port.
#include <cstdio>
#include <vector>

#include "Vrouter.h"
#include "noc_tb.h"
#include "verilated.h"

static const int PN = 0, PE = 1, PS = 2, PW = 3, PL = 4;

struct Dut {
  Vrouter top;
  void tick() {
    top.clk = 0; top.eval();
    top.clk = 1; top.eval();
  }
  void reset() {
    top.rst_n = 0;
    for (int p = 0; p < 5; p++) {
      top.in_valid[p] = 0; top.in_flit[p] = 0; top.out_credit_i[p] = 0;
    }
    tick(); tick();
    top.rst_n = 1;
    tick();
  }
  void clear_inputs() {
    for (int p = 0; p < 5; p++) {
      top.in_valid[p] = 0; top.out_credit_i[p] = 0;
    }
  }
};

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Dut d;

  // ---------- Test A: collision -> one wins East, one deflects ----------
  d.reset();
  Flit a; a.dst_x = 3; a.dst_y = 1; a.payload = 0xA;   // East-bound
  Flit b; b.dst_x = 3; b.dst_y = 1; b.payload = 0xB;   // East-bound
  d.top.in_valid[PN] = 1; d.top.in_flit[PN] = encode(a);
  d.top.in_valid[PW] = 1; d.top.in_flit[PW] = encode(b);
  d.tick();              // flits land in FIFOs
  d.clear_inputs();
  d.top.eval();          // allocation is combinational on FIFO state

  int east = 0, elsewhere = 0;
  uint16_t east_payload = 0;
  for (int q = 0; q < 5; q++) {
    if (!d.top.out_valid[q]) continue;
    Flit f = decode(d.top.out_flit[q]);
    CHECK(f.dst_x == 3 && f.dst_y == 1, "A: flit fields corrupted");
    if (q == PE) { east++; east_payload = f.payload; }
    else         { elsewhere++; CHECK(q != PL, "A: non-local flit ejected"); }
  }
  CHECK(east == 1, "A: exactly one flit must win the East port");
  CHECK(elsewhere == 1, "A: the loser must be deflected, not stalled");
  CHECK(east_payload == 0xA || east_payload == 0xB, "A: payload corrupted");
  printf("PASS Test A: collision -> 1 routed East, 1 deflected same cycle\n");

  // ---------- Test B: credit exhaustion -> deflect, then stall ----------
  d.reset();
  // 10 East-bound flits, never return any credits. Capacity without returns:
  // 2 credits x 4 directions = 8 sends; flits 9,10 must stall inside.
  int sent_in = 0, seen_out = 0, seen_east = 0;
  std::vector<int> out_per_port(5, 0);
  for (int cycle = 0; cycle < 40; cycle++) {
    d.clear_inputs();
    if (sent_in < 10) {
      Flit f; f.dst_x = 3; f.dst_y = 1; f.payload = (uint16_t)(0x100 + sent_in);
      d.top.in_valid[PN] = 1; d.top.in_flit[PN] = encode(f);
      sent_in++;
    }
    d.tick();
    for (int q = 0; q < 5; q++)
      if (d.top.out_valid[q]) { seen_out++; out_per_port[q]++; if (q == PE) seen_east++; }
  }
  CHECK(sent_in == 10, "B: all 10 flits injected");
  CHECK(seen_east == 2, "B: East must carry exactly its 2 credits");
  CHECK(seen_out == 8, "B: 8 sends then hard stall (2 credits x 4 dirs)");
  CHECK(out_per_port[PL] == 0, "B: nothing may eject (non-local dest)");

  // Return credits; everything stalled must drain, nothing lost.
  for (int cycle = 0; cycle < 60 && seen_out < 10; cycle++) {
    d.clear_inputs();
    for (int q = 0; q < 4; q++) d.top.out_credit_i[q] = 1;  // refill dirs
    d.tick();
    for (int q = 0; q < 5; q++) if (d.top.out_valid[q]) seen_out++;
  }
  CHECK(seen_out == 10, "B: all 10 flits must eventually leave (no loss)");
  printf("PASS Test B: credits exhausted -> deflect then stall; refill -> drain, 10/10 conserved\n");

  // ---------- Test C: ejection on the Local port ----------
  d.reset();
  Flit c; c.dst_x = 1; c.dst_y = 1; c.payload = 0xEE;  // addressed to (1,1)
  d.top.in_valid[PS] = 1; d.top.in_flit[PS] = encode(c);
  d.tick();
  d.clear_inputs();
  d.top.eval();
  CHECK(d.top.out_valid[PL], "C: locally-addressed flit must eject");
  CHECK(decode(d.top.out_flit[PL]).payload == 0xEE, "C: eject payload intact");
  for (int q = 0; q < 4; q++) CHECK(!d.top.out_valid[q], "C: no stray output");
  printf("PASS Test C: ejection on Local port, payload intact\n");

  printf("tb_router: ALL TESTS PASSED\n");
  return 0;
}
