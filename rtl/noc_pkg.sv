// Shared types for the NoC.
//
// The flit is deliberately <= 64 bits so Verilator exposes each port as a
// plain uint64_t to the C++ testbench. Every flit carries full routing
// information (BLESS-style flit-level switching): under deflection, flits of
// one packet may take different paths, so each must be self-routing and the
// destination NI reassembles by (src, pkt_id, seq).
package noc_pkg;

  parameter int unsigned XW   = 4;   // mesh coordinate width (up to 16x16)
  parameter int unsigned YW   = 4;
  parameter int unsigned PIDW = 8;   // packet id
  parameter int unsigned SEQW = 4;   // flit sequence within packet (up to 16)
  parameter int unsigned AGEW = 8;   // age for oldest-first arbitration
  parameter int unsigned PLW  = 16;  // payload bits per flit

  typedef struct packed {
    logic [XW-1:0]   dst_x;
    logic [YW-1:0]   dst_y;
    logic [XW-1:0]   src_x;
    logic [YW-1:0]   src_y;
    logic [PIDW-1:0] pkt_id;
    logic [SEQW-1:0] seq;
    logic            last;    // tail flit marker
    logic [AGEW-1:0] age;     // hops + cycles waited (saturating)
    logic [PLW-1:0]  payload;
  } flit_t;

  localparam int unsigned FLIT_W = $bits(flit_t);  // 53

  // Port indices: North, East, South, West, Local
  localparam int unsigned PN = 0;
  localparam int unsigned PE = 1;
  localparam int unsigned PS = 2;
  localparam int unsigned PW = 3;
  localparam int unsigned PL = 4;
  localparam int unsigned NPORTS = 5;

endpackage
