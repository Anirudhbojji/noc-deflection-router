// Parameterized NX x NY 2D mesh of deflection routers.
//
// Local (inject/eject) ports of every node are exposed at the top level,
// flattened as node index n = y*NX + x. Edge ports are tied off; the routers'
// PORT_EXISTS masks guarantee nothing is ever routed off-mesh.

module mesh
  import noc_pkg::*;
#(
  parameter int unsigned NX    = 4,
  parameter int unsigned NY    = 4,
  parameter int unsigned DEPTH = 2,
  localparam int unsigned NNODES = NX * NY
)(
  input  logic clk,
  input  logic rst_n,

  // injection (testbench/NI -> router local input port)
  input  logic [FLIT_W-1:0] inj_flit   [NNODES],
  input  logic              inj_valid  [NNODES],
  output logic              inj_credit [NNODES],  // pulses: local FIFO slot freed

  // ejection (router local output port -> testbench/NI)
  output logic [FLIT_W-1:0] ej_flit    [NNODES],
  output logic              ej_valid   [NNODES],
  input  logic              ej_credit  [NNODES]   // pulses: consumer freed a slot
);

  // Per-node port arrays
  flit_t r_in_flit    [NNODES][NPORTS];
  logic  r_in_valid   [NNODES][NPORTS];
  logic  r_in_credit  [NNODES][NPORTS];
  flit_t r_out_flit   [NNODES][NPORTS];
  logic  r_out_valid  [NNODES][NPORTS];
  logic  r_out_credit [NNODES][NPORTS];

  genvar gx, gy;
  generate
    for (gy = 0; gy < NY; gy++) begin : g_row
      for (gx = 0; gx < NX; gx++) begin : g_col
        localparam int unsigned N = gy * NX + gx;

        router #(
          .MY_X(gx), .MY_Y(gy), .NX(NX), .NY(NY), .DEPTH(DEPTH)
        ) u_router (
          .clk          (clk),
          .rst_n        (rst_n),
          .in_flit      (r_in_flit[N]),
          .in_valid     (r_in_valid[N]),
          .in_credit_o  (r_in_credit[N]),
          .out_flit     (r_out_flit[N]),
          .out_valid    (r_out_valid[N]),
          .out_credit_i (r_out_credit[N])
        );

        // ----- local port <-> top level -----
        assign r_in_flit[N][PL]     = flit_t'(inj_flit[N]);
        assign r_in_valid[N][PL]    = inj_valid[N];
        assign inj_credit[N]        = r_in_credit[N][PL];
        assign ej_flit[N]           = FLIT_W'(r_out_flit[N][PL]);
        assign ej_valid[N]          = r_out_valid[N][PL];
        assign r_out_credit[N][PL]  = ej_credit[N];

        // ----- north link (to node at gy-1): my PN out -> their PS in -----
        if (gy > 0) begin : g_north
          localparam int unsigned UP = (gy - 1) * NX + gx;
          assign r_in_flit[UP][PS]    = r_out_flit[N][PN];
          assign r_in_valid[UP][PS]   = r_out_valid[N][PN];
          assign r_out_credit[N][PN]  = r_in_credit[UP][PS];
        end else begin : g_north_edge
          assign r_out_credit[N][PN] = 1'b0;
          assign r_in_flit[N][PN]    = '0;
          assign r_in_valid[N][PN]   = 1'b0;
        end

        // ----- south link (to node at gy+1): my PS out -> their PN in -----
        if (gy < NY - 1) begin : g_south
          localparam int unsigned DN = (gy + 1) * NX + gx;
          assign r_in_flit[DN][PN]    = r_out_flit[N][PS];
          assign r_in_valid[DN][PN]   = r_out_valid[N][PS];
          assign r_out_credit[N][PS]  = r_in_credit[DN][PN];
        end else begin : g_south_edge
          assign r_out_credit[N][PS] = 1'b0;
          assign r_in_flit[N][PS]    = '0;
          assign r_in_valid[N][PS]   = 1'b0;
        end

        // ----- east link (to node at gx+1): my PE out -> their PW in -----
        if (gx < NX - 1) begin : g_east
          localparam int unsigned RT = gy * NX + gx + 1;
          assign r_in_flit[RT][PW]    = r_out_flit[N][PE];
          assign r_in_valid[RT][PW]   = r_out_valid[N][PE];
          assign r_out_credit[N][PE]  = r_in_credit[RT][PW];
        end else begin : g_east_edge
          assign r_out_credit[N][PE] = 1'b0;
          assign r_in_flit[N][PE]    = '0;
          assign r_in_valid[N][PE]   = 1'b0;
        end

        // ----- west link (to node at gx-1): my PW out -> their PE in -----
        if (gx > 0) begin : g_west
          localparam int unsigned LT = gy * NX + gx - 1;
          assign r_in_flit[LT][PE]    = r_out_flit[N][PW];
          assign r_in_valid[LT][PE]   = r_out_valid[N][PW];
          assign r_out_credit[N][PW]  = r_in_credit[LT][PE];
        end else begin : g_west_edge
          assign r_out_credit[N][PW] = 1'b0;
          assign r_in_flit[N][PW]    = '0;
          assign r_in_valid[N][PW]   = 1'b0;
        end
      end
    end
  endgenerate

endmodule
