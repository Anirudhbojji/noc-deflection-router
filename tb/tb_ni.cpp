// Unit test for the network interface (packet -> flit serializer).
//
// With only 2 credits and no returns, a 3-flit packet must emit flits
// seq 0,1 then stall; returning one credit releases seq 2 with last=1.
#include <cstdio>

#include "Vni.h"
#include "noc_tb.h"
#include "verilated.h"

struct Dut {
  Vni top;
  void tick() {
    top.clk = 0; top.eval();
    top.clk = 1; top.eval();
  }
};

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Dut d;

  d.top.rst_n = 0; d.top.pkt_valid = 0; d.top.credit_i = 0;
  d.tick(); d.tick();
  d.top.rst_n = 1; d.tick();

  CHECK(d.top.pkt_ready, "NI must be ready after reset");

  // Offer a 3-flit packet to (2,3), payload base 0x50.
  d.top.pkt_valid = 1;
  d.top.pkt_dst_x = 2; d.top.pkt_dst_y = 3;
  d.top.pkt_len = 3; d.top.pkt_payload = 0x50;
  d.tick();
  d.top.pkt_valid = 0;
  CHECK(!d.top.pkt_ready, "NI busy while serializing");

  int got = 0;
  Flit seen[3];
  // Two flits flow on the 2 initial credits...
  for (int cycle = 0; cycle < 4 && got < 2; cycle++) {
    d.top.eval();
    if (d.top.flit_valid_o) seen[got++] = decode(d.top.flit_o);
    d.tick();
  }
  CHECK(got == 2, "exactly 2 flits on 2 credits");
  // ...then it must stall with credits exhausted.
  for (int cycle = 0; cycle < 3; cycle++) {
    d.top.eval();
    CHECK(!d.top.flit_valid_o, "NI must stall with 0 credits");
    d.tick();
  }
  // Return one credit -> final flit is released.
  d.top.credit_i = 1; d.tick(); d.top.credit_i = 0;
  for (int cycle = 0; cycle < 4 && got < 3; cycle++) {
    d.top.eval();
    if (d.top.flit_valid_o) seen[got++] = decode(d.top.flit_o);
    d.tick();
  }
  CHECK(got == 3, "third flit released after credit return");

  for (int i = 0; i < 3; i++) {
    CHECK(seen[i].seq == i, "flit sequence order");
    CHECK(seen[i].dst_x == 2 && seen[i].dst_y == 3, "flit destination");
    CHECK(seen[i].payload == 0x50 + i, "per-flit payload");
    CHECK(seen[i].last == (i == 2 ? 1 : 0), "tail marker on last flit only");
    CHECK(seen[i].pkt_id == 0, "first packet id is 0");
  }
  d.top.eval();
  CHECK(d.top.pkt_ready, "NI ready again after tail flit");

  printf("tb_ni: ALL TESTS PASSED (head/body/tail framing + credit stall)\n");
  return 0;
}
