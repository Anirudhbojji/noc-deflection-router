// Network interface: packet -> flit serializer with credit handling.
//
// Accepts a packet (destination + length + payload base) and streams it into
// the router's local port as a head/body/tail flit sequence, respecting
// credit-based backpressure. Because deflection may reorder flits in flight,
// every flit carries (src, pkt_id, seq, last) and the consumer reassembles.

module ni
  import noc_pkg::*;
#(
  parameter int unsigned MY_X    = 0,
  parameter int unsigned MY_Y    = 0,
  parameter int unsigned DEPTH   = 2,   // credits toward the router local FIFO
  parameter int unsigned MAX_LEN = 8
)(
  input  logic clk,
  input  logic rst_n,

  // packet interface (valid/ready)
  input  logic              pkt_valid,
  output logic              pkt_ready,
  input  logic [XW-1:0]     pkt_dst_x,
  input  logic [YW-1:0]     pkt_dst_y,
  input  logic [SEQW-1:0]   pkt_len,      // flits in packet, >= 1
  input  logic [PLW-1:0]    pkt_payload,  // payload of flit i = payload + i

  // flit interface toward router local input port
  output flit_t             flit_o,
  output logic              flit_valid_o,
  input  logic              credit_i      // pulse: router freed a local slot
);

  logic [$clog2(DEPTH+1)-1:0] credits;
  logic [PIDW-1:0]            next_pkt_id;

  logic                busy;
  logic [SEQW-1:0]     seq, len;
  logic [XW-1:0]       dst_x;
  logic [YW-1:0]       dst_y;
  logic [PLW-1:0]      payload;
  logic [PIDW-1:0]     pid;

  assign pkt_ready = !busy;

  logic send;
  assign send = busy && (credits != 0);

  always_comb begin
    flit_o         = '0;
    flit_o.dst_x   = dst_x;
    flit_o.dst_y   = dst_y;
    flit_o.src_x   = XW'(MY_X);
    flit_o.src_y   = YW'(MY_Y);
    flit_o.pkt_id  = pid;
    flit_o.seq     = seq;
    flit_o.last    = (seq == len - 1'b1);
    flit_o.age     = '0;
    flit_o.payload = payload + PLW'(seq);
    flit_valid_o   = send;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      credits     <= ($clog2(DEPTH+1))'(DEPTH);
      next_pkt_id <= '0;
      busy        <= 1'b0;
      seq         <= '0;
      len         <= '0;
      dst_x       <= '0;
      dst_y       <= '0;
      payload     <= '0;
      pid         <= '0;
    end else begin
      credits <= credits - ($clog2(DEPTH+1))'(send)
                         + ($clog2(DEPTH+1))'(credit_i);
      if (!busy && pkt_valid) begin
        busy        <= 1'b1;
        seq         <= '0;
        len         <= (pkt_len == 0) ? SEQW'(1) : pkt_len;
        dst_x       <= pkt_dst_x;
        dst_y       <= pkt_dst_y;
        payload     <= pkt_payload;
        pid         <= next_pkt_id;
        next_pkt_id <= next_pkt_id + 1'b1;
      end else if (send) begin
        if (seq == len - 1'b1) busy <= 1'b0;
        else                   seq  <= seq + 1'b1;
      end
    end
  end

endmodule
