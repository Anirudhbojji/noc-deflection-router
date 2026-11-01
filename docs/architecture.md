# Architecture notes

## Why deflection routing?

A conventional virtual-channel router spends most of its silicon on input
buffers — in published NoC designs, buffers account for roughly 30–75% of
router area and a comparable share of leakage power. Deflection (hot-potato)
routing attacks that cost directly: when two flits want the same output port,
the loser is *misrouted* to any free port instead of being queued. Misrouted
flits take extra hops, but no large buffers are needed and nothing is dropped.

This design is a pragmatic hybrid:

- **tiny input FIFOs (DEPTH=2)** per port, protected by credit-based flow
  control — enough elasticity to decouple link timing, far below VC-router
  buffer budgets;
- **deflection-first contention handling** — a flit only stalls in its FIFO
  when *every* direction is simultaneously busy or credit-starved, which the
  load sweep shows is rare below saturation.

## Routing pipeline (one hop per cycle)

```
FIFO heads ──> XY route compute ──> oldest-first allocation ──> port grants
                                        │ preferred port busy?
                                        └──> deflect to any free direction
```

1. **XY (dimension-ordered) routing** computes the preferred output: resolve X
   before Y; eject on the Local port when both match. XY is deadlock-free on
   a mesh for its *productive* moves.
2. **Oldest-first allocation**: input heads are served in descending `age`
   order (age = hops + cycles waited, saturating). Ties break on port index.
3. **Deflection**: a loser takes the first free direction in a fixed scan
   order. Non-local flits are never ejected; local-destined flits that lose
   ejection arbitration bounce back into the mesh and retry.

## Why flit-level switching instead of classic wormhole

Classic wormhole switching reserves a path for the whole worm (head locks the
route, body/tail follow). Under deflection the head's path cannot be reserved
— any flit may be bounced at any router — so a contiguous worm cannot be
maintained. Like the BLESS/CHIPPER family of bufferless routers, every flit
carries its full routing state (`dst`, `src`, `pkt_id`, `seq`, `last`) and the
destination NI reassembles packets; head/body/tail framing is preserved
end-to-end even when flits arrive out of order. The testbench's Phase 1
verifies exactly this property.

## Livelock freedom (argument)

Deflection can, in principle, bounce a flit forever. The standard
countermeasure, used here, is **oldest-first arbitration with a monotonic age
field**: the globally oldest flit in the network wins allocation at whatever
router it occupies, therefore always makes its productive XY move, therefore
reaches its destination in bounded time; once it ejects, the next-oldest flit
inherits the guarantee (induction on age). The load sweep's drain check —
every injected flit must be delivered after injection stops, at every load
point — is the empirical backstop for this argument (hundreds of thousands of
flits, zero stuck).

## Deadlock safety

Credits only ever *delay* a flit inside a FIFO; they never create a routing
cycle, because a stalled head re-arbitrates every cycle and may leave on any
free port (deflection breaks the circular-wait condition that buffer-full
wormhole networks suffer from). The ejection port drains unconditionally as
long as the NI returns credits.

## Scale math (the "terabit" claim, honestly)

The RTL is parameterized in mesh size and payload width. The simulated
configuration is deliberately small (4×4, 53-bit flits) so regressions run in
seconds. A synthesis-oriented configuration of the same microarchitecture:

| Parameter | Value |
|---|---|
| Mesh | 8×8 (64 nodes) |
| Flit/link width | 256 bit |
| Target clock | 1.5 GHz (typical NoC speeds in 7–16 nm) |
| Per-link bandwidth | 384 Gb/s |
| Bisection links | 8 columns × 2 directions = 16 |
| **Bisection bandwidth** | **~6.1 Tb/s** |
| Aggregate router switching | 64 nodes × 5 ports × 384 Gb/s ≈ 123 Tb/s |

The simulation proves the *logic* (allocation, deflection, credits, delivery);
the table states what the *architecture* scales to — not a measured silicon
result.
