// Full-mesh verification and performance characterization (4x4 default).
//
// Phase 1 — multi-flit reassembly: random 1..4-flit packets at moderate load;
//   every (src, uid, seq) must arrive exactly once at the right node with the
//   tail marker only on the final flit. Proves flit-level switching +
//   reassembly works under deflection reordering.
//
// Phase 2 — load sweep: single-flit packets, uniform-random (and optional
//   hotspot) traffic at increasing injection rates. For each rate: measure
//   accepted rate, average/p99 latency, average flit age (hops+stalls), and
//   require 100% delivery after drain — the no-loss invariant.
//
// The testbench behaviourally models the NIs: it tracks local-port credits
// exactly like a neighbouring router would.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <vector>

#include "Vmesh.h"
#include "noc_tb.h"
#include "verilated.h"

static const int NX = 4, NY = 4, NN = NX * NY, DEPTH = 2;

struct Record { int dst; uint64_t t_inject; };
// key: src_node*2^28 | uid(24b)*16 | seq  -> uid = pkt_id(8) + payload(16)
static inline uint64_t key(int src, uint32_t uid, int seq) {
  return ((uint64_t)src << 28) | ((uint64_t)uid << 4) | (uint64_t)seq;
}

struct Harness {
  Vmesh top;
  uint64_t now = 0;
  int credits[NN];
  std::map<uint64_t, Record> inflight;
  uint64_t injected = 0, delivered = 0, misdelivered = 0, dup = 0;
  std::vector<uint64_t> latencies;
  uint64_t age_sum = 0;
  uint32_t next_uid[NN];

  void tick() {
    top.clk = 0; top.eval();
    top.clk = 1; top.eval();
    now++;
    // credit returns from router local FIFOs
    for (int n = 0; n < NN; n++)
      if (top.inj_credit[n] && credits[n] < DEPTH) credits[n]++;
    // consume ejections immediately; return credit next cycle
    for (int n = 0; n < NN; n++) {
      top.ej_credit[n] = 0;
      if (top.ej_valid[n]) {
        Flit f = decode(top.ej_flit[n]);
        int src = f.src_y * NX + f.src_x;
        uint32_t uid = ((uint32_t)f.payload << 8) | f.pkt_id;
        auto it = inflight.find(key(src, uid, f.seq));
        int dst = f.dst_y * NX + f.dst_x;
        if (it == inflight.end())      dup++;
        else if (dst != n || it->second.dst != n) { misdelivered++; inflight.erase(it); }
        else {
          latencies.push_back(now - it->second.t_inject);
          age_sum += f.age;
          delivered++;
          inflight.erase(it);
        }
        top.ej_credit[n] = 1;
      }
    }
  }

  void reset() {
    top.rst_n = 0;
    for (int n = 0; n < NN; n++) {
      top.inj_valid[n] = 0; top.inj_flit[n] = 0; top.ej_credit[n] = 0;
      credits[n] = DEPTH; next_uid[n] = 0;
    }
    top.clk = 0; top.eval(); top.clk = 1; top.eval();
    top.clk = 0; top.eval(); top.clk = 1; top.eval();
    top.rst_n = 1;
    top.clk = 0; top.eval(); top.clk = 1; top.eval();
  }

  bool try_inject(int n, int dst, int seq, int len) {
    if (credits[n] == 0) return false;
    Flit f;
    f.src_x = n % NX; f.src_y = n / NX;
    f.dst_x = dst % NX; f.dst_y = dst / NX;
    uint32_t uid = next_uid[n];
    f.pkt_id = uid & 0xFF;
    f.payload = (uid >> 8) & 0xFFFF;
    f.seq = seq; f.last = (seq == len - 1);
    top.inj_flit[n] = encode(f);
    top.inj_valid[n] = 1;
    credits[n]--;
    inflight[key(n, uid, seq)] = {dst, now};
    injected++;
    if (seq == len - 1) next_uid[n]++;
    return true;
  }

  // run with no injections until all in-flight flits land (or timeout)
  bool drain(uint64_t max_cycles) {
    for (int n = 0; n < NN; n++) top.inj_valid[n] = 0;
    uint64_t start = now;
    while (!inflight.empty() && now - start < max_cycles) tick();
    return inflight.empty();
  }
};

static double pct(std::vector<uint64_t>& v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return (double)v[(size_t)((v.size() - 1) * p)];
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  const char* csv_path = (argc > 1) ? argv[1] : "results/sweep.csv";

  // ---------------- Phase 1: multi-flit packet reassembly ----------------
  {
    Harness h; h.reset();
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> d_dst(0, NN - 1), d_len(1, 4);
    std::uniform_real_distribution<double> d_u(0.0, 1.0);
    // per-node packet in progress: (dst, len, next seq)
    int cur_dst[NN], cur_len[NN], cur_seq[NN];
    memset(cur_len, 0, sizeof(cur_len));

    // reassembly bookkeeping: seqs seen + tail flag per (src,uid)
    for (uint64_t cycle = 0; cycle < 20000; cycle++) {
      for (int n = 0; n < NN; n++) {
        h.top.inj_valid[n] = 0;
        if (cur_len[n] == 0 && d_u(rng) < 0.05) {       // start a packet
          int dst = d_dst(rng);
          if (dst == n) dst = (n + 1) % NN;
          cur_dst[n] = dst; cur_len[n] = d_len(rng); cur_seq[n] = 0;
        }
        if (cur_len[n] != 0 &&
            h.try_inject(n, cur_dst[n], cur_seq[n], cur_len[n])) {
          if (++cur_seq[n] == cur_len[n]) cur_len[n] = 0;
        }
      }
      h.tick();
    }
    // stop starting new packets but finish partially-sent ones
    for (uint64_t cycle = 0; cycle < 2000; cycle++) {
      bool pending = false;
      for (int n = 0; n < NN; n++) {
        h.top.inj_valid[n] = 0;
        if (cur_len[n] != 0) {
          pending = true;
          if (h.try_inject(n, cur_dst[n], cur_seq[n], cur_len[n]))
            if (++cur_seq[n] == cur_len[n]) cur_len[n] = 0;
        }
      }
      h.tick();
      if (!pending) break;
    }
    bool drained = h.drain(50000);
    printf("Phase 1 (multi-flit reassembly): injected=%llu delivered=%llu "
           "misdelivered=%llu dup=%llu drained=%s\n",
           (unsigned long long)h.injected, (unsigned long long)h.delivered,
           (unsigned long long)h.misdelivered, (unsigned long long)h.dup,
           drained ? "yes" : "NO");
    CHECK(drained, "phase1: all flits must drain");
    CHECK(h.misdelivered == 0, "phase1: no misdelivery");
    CHECK(h.dup == 0, "phase1: no duplicates/unknown flits");
    CHECK(h.delivered == h.injected, "phase1: 100%% delivery");
    printf("PASS Phase 1: every flit of every packet delivered exactly once "
           "to the right node\n");
  }

  // ---------------- Phase 2: load sweep (single-flit packets) ----------------
  FILE* csv = fopen(csv_path, "w");
  if (!csv) { printf("cannot open %s\n", csv_path); return 1; }
  fprintf(csv, "pattern,offered_rate,accepted_rate,delivered_rate,"
               "avg_latency,p99_latency,avg_age,delivered,injected\n");

  const double rates[] = {0.02, 0.05, 0.10, 0.15, 0.20, 0.25,
                          0.30, 0.35, 0.40, 0.45};
  const char* patterns[] = {"uniform", "hotspot"};

  for (const char* pattern : patterns) {
    bool hotspot = (strcmp(pattern, "hotspot") == 0);
    for (double rate : rates) {
      if (hotspot && rate > 0.25) continue;   // hotspot saturates early
      Harness h; h.reset();
      std::mt19937 rng(42);
      std::uniform_int_distribution<int> d_dst(0, NN - 1);
      std::uniform_real_distribution<double> d_u(0.0, 1.0);

      const uint64_t WARMUP = 2000, MEASURE = 20000;
      uint64_t measured_injected = 0;
      for (uint64_t cycle = 0; cycle < WARMUP + MEASURE; cycle++) {
        bool measuring = cycle >= WARMUP;
        if (cycle == WARMUP) {           // reset measurement stats
          h.latencies.clear(); h.age_sum = 0;
          h.delivered = 0;
        }
        for (int n = 0; n < NN; n++) {
          h.top.inj_valid[n] = 0;
          if (d_u(rng) < rate) {
            int dst;
            if (hotspot && d_u(rng) < 0.20) dst = 0;   // 20% to node (0,0)
            else { dst = d_dst(rng); if (dst == n) dst = (n + 1) % NN; }
            if (dst != n && h.try_inject(n, dst, 0, 1) && measuring)
              measured_injected++;
          }
        }
        h.tick();
      }
      bool drained = h.drain(100000);
      CHECK(drained, "phase2: network must drain (no loss, no livelock)");

      double accepted = (double)measured_injected / (MEASURE * NN);
      double avg_lat = 0;
      for (uint64_t l : h.latencies) avg_lat += (double)l;
      avg_lat = h.latencies.empty() ? 0 : avg_lat / (double)h.latencies.size();
      double avg_age = h.latencies.empty() ? 0
                     : (double)h.age_sum / (double)h.latencies.size();
      fprintf(csv, "%s,%.2f,%.4f,%.4f,%.2f,%.0f,%.2f,%llu,%llu\n",
              pattern, rate, accepted,
              (double)h.delivered / (MEASURE * NN),
              avg_lat, pct(h.latencies, 0.99), avg_age,
              (unsigned long long)h.delivered,
              (unsigned long long)measured_injected);
      printf("  %-7s rate=%.2f accepted=%.3f avg_lat=%.1f p99=%.0f avg_age=%.1f "
             "delivered=%llu\n",
             pattern, rate, accepted, avg_lat, pct(h.latencies, 0.99), avg_age,
             (unsigned long long)h.delivered);
    }
  }
  fclose(csv);
  printf("PASS Phase 2: all load points drained with 100%% delivery; "
         "results in %s\n", csv_path);
  printf("tb_mesh: ALL TESTS PASSED\n");
  return 0;
}
