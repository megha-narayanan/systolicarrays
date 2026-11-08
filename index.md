# Project Proposal

## TITLE
**Systolic Arrays for LLM Inference: Parallel Efficiency of Prefill vs. Decode on a Parameterized INT8 Accelerator**

Raj Methi and Megha Narayanan

## URL
https://megha-narayanan.github.io/systolicarrays/

## SUMMARY
We will build a parameterized INT8 output-stationary systolic array in SystemVerilog and simulate it cycle-accurately with Verilator, driven by a C++ host runtime with a bandwidth-limited memory model. Using matrix shapes from a real LLM, we will measure how well the array's parallelism is used in compute-bound prefill versus memory-bound decode, and whether one large array or several smaller ones makes better use of a fixed compute budget once memory bandwidth and real FPGA clock speed are accounted for. For decode, we will also measure how batching and 4-bit weights reduce the memory bottleneck, and how KV-cache traffic limits both as batch size and context length grow. We will compare against cuBLAS INT8 on the GHC GPUs.

## BACKGROUND
LLM inference is dominated by matrix multiplies \(C = A \times B\) (activations × weights), which appear in two phases:

- **Prefill** processes the whole prompt at once. \(A\) has \(M\) = (number of prompt tokens) rows, so every weight is reused \(M\) times, giving large, compute-bound GEMMs.
- **Decode** generates one token at a time. At batch size 1, \(M = 1\) (a matrix-vector product): each INT8 weight is used for exactly one multiply-accumulate (MAC), so performance is limited by how fast weights arrive from memory. Batching \(B\) sequences reuses each weight \(B\) times, but attention also reads each sequence's own **KV cache** (stored keys and values of all previous tokens), which cannot be shared across the batch.

A systolic array is a 2D grid of processing elements (PEs) that pass operands to their neighbors instead of fetching them from a shared memory. In our **output-stationary** design:

- PE \((i,j)\) owns output \(C(i,j)\) in a local 32-bit accumulator, so partial sums never leave the PE.
- Row \(i\) of \(A\) streams in from the left; column \(j\) of \(B\) streams in from the top.
- Inputs are skewed by one cycle per row/column, so \(A(i,k)\) and \(B(k,j)\) meet at PE \((i,j)\) on cycle \(t = i + j + k\).

```
          B[k,0]    B[k,1]    B[k,2]      (column j delayed j cycles)
            v         v         v
A[0,k] -> [PE00] -> [PE01] -> [PE02]
            v         v         v
A[1,k] -> [PE10] -> [PE11] -> [PE12]      (row i delayed i cycles)
            v         v         v
A[2,k] -> [PE20] -> [PE21] -> [PE22]
```

Every value entering an \(N \times N\) array is reused by \(N\) PEs, so the array performs \(N^2\) MACs per cycle while reading only \(2N\) bytes. This data reuse (high arithmetic intensity) is what makes systolic arrays efficient, and weight reuse is exactly what decode lacks.

## THE CHALLENGE

### Workload
- **Fill and drain.** Because inputs are skewed, PEs far from the top-left corner start late and PEs near it finish early. For an \(N \times N\) array and a tile of depth \(K\), compute takes about \(K + 2N - 2\) cycles, so utilization is roughly \(K / (K + 2N - 2)\) before the cost of draining results. Small tiles make this worse.
- **Decode shape mismatch.** With \(M = 1\), only one of the \(N\) rows has work, so utilization is at most \(1/N\). Even a fully busy array cannot outrun the rate at which weights arrive from memory.
- **Tiling and data movement.** Real layers are far larger than the array, so the host must tile them and stage operands in on-chip buffers. If loading the next tile cannot overlap with computing the current one, PEs stall.
- **Two kinds of decode traffic.** Each decode step reads all weights (about 8 GB at INT8 for an 8B model), shared across the batch, plus each sequence's KV cache (about 64 KB per token of context at INT8 for Llama 3 8B, so about 256 MB per sequence at 4,096 tokens). At batch 32 and 4,096-token contexts the two are roughly equal, so batching and weight quantization both stop helping once KV traffic dominates.

### System constraints
- **Reuse vs. bandwidth when scaling out.** Splitting a fixed compute budget into smaller independent arrays reduces reuse, so each MAC needs more memory bandwidth:

  | Configuration | PEs | Bytes in per cycle | MACs per byte |
  |---|---|---|---|
  | 1 × 32×32 | 1024 | 64 | 16 |
  | 4 × 16×16 | 1024 | 128 | 8 |
  | 16 × 8×8 | 1024 | 256 | 4 |

  Smaller arrays also lose less time to fill and drain (smaller \(N\)). For decode, the tradeoff flips: with \(M = 1\), an array absorbs at most one weight per column per cycle, so sixteen 8×8 arrays can consume 128 weights per cycle versus 32 for one 32×32 array.
- **Clock speed.** Because a systolic array only wires PEs to their neighbors, its clock speed should in principle not degrade much as it grows. We expect any slowdown to come from signals that reach every PE (stall/enable, reset) and from the result readout path. We will measure this with post-place-and-route timing rather than assume it.

**Hypotheses:** (1) The best configuration depends mostly on available bandwidth and workload phase (larger arrays favored for prefill, smaller arrays for decode), with clock-speed differences playing a smaller role. (2) Weight-only INT4 roughly doubles decode throughput at small batch sizes and short contexts, but the gain shrinks as KV-cache traffic grows. Our experiments are designed to test both.

## RESOURCES
- **RTL and simulation:** SystemVerilog and Verilator (cycle-accurate C++ model of the RTL).
- **Synthesis:** AMD Vivado (free edition) for post-place-and-route Fmax and LUT/DSP usage. In Week 1 we will choose one target FPGA part with enough DSP blocks for the largest configuration.
- **GPU baseline:** NVIDIA GPUs in the GHC labs (cuBLAS INT8).
- **Workloads:** attention-projection and feed-forward GEMM shapes from an open ~8B-parameter LLM (e.g., Llama 3 8B), with prefill \(M \in \{128, 512, 2048\}\) and decode batch sizes \(M \in \{1, 2, 4, \ldots, 64\}\). For decode we also include attention against the KV cache (\(QK^T\) and scores × \(V\)) at context lengths \(L \in \{512, 2048, 8192\}\).
- **Code:** written from scratch (RTL, C++ host runtime, golden model, analytical model). The RTL is limited to the PE, array, input skew, result drain, control, and counters. Batching, INT4 weights, KV-cache attention, and load/compute overlap are handled in the C++ harness and memory model, so they add no new RTL.
- **Special resources:** none required. An FPGA board is needed only for the 125% goal; we will ask course staff.
- **References:**
  - H. T. Kung, "Why Systolic Architectures?", *IEEE Computer*, 1982.
  - N. P. Jouppi et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit," ISCA 2017.
  - A. Samajdar et al., "SCALE-Sim: Systolic CNN Accelerator Simulator," arXiv, 2018.
  - J. Wang, L. Guo, J. Cong, "AutoSA: A Polyhedral Compiler for High-Performance Systolic Arrays on FPGA," FPGA 2021.
  - W. Kwon et al., "Efficient Memory Management for Large Language Model Serving with PagedAttention," SOSP 2023.
  - E. Frantar et al., "GPTQ: Accurate Post-Training Quantization for Generative Pre-trained Transformers," ICLR 2023.

## GOALS AND DELIVERABLES

### 75% (must have)
- Parameterized INT8 output-stationary array (8×8 to 32×32), verified bit-exact against a C++ golden model in Verilator.
- C++ host runtime that tiles arbitrary matrices, applies input skew, and reads PE-activity counters (which can be compiled out so they do not affect synthesis results).
- Cycle counts and utilization for prefill and decode shapes, compared against the \(K / (K + 2N - 2)\) prediction (for example, about 94% for \(N = 16\) and about 89% for \(N = 32\) at \(K = 512\)).
- cuBLAS INT8 baseline on GHC GPUs for the same shapes.

### 100% (plan to achieve)
- Bandwidth-limited memory model in the harness (configurable bytes/cycle and latency), including overlapped loading of the next tile while the current one computes.
- Roofline analysis of prefill vs. decode, including how much decode batching (\(M = 1\) to \(64\)) recovers.
- Scale-up vs. scale-out sweep (1×32×32, 4×16×16, 16×8×8) at equal total memory bandwidth, combining simulated cycle counts with post-place-and-route Fmax to get effective throughput.
- Analytical cycle model validated against simulation (target: within 10%), used to extrapolate full-size layers that are too slow to simulate in full.
- Decode with KV-cache attention and weight-only INT4 (4-bit weights unpacked to INT8 before entering the array; activations and KV cache stay INT8), swept over batch size and context length.

### 125% (hope to achieve)
- Run the array on a real FPGA board and measure latency and power.
- Custom CUDA Tensor Core (WMMA) kernel as a second GPU comparison.
- Double-buffered operand buffers and the INT4 unpacker in RTL, replacing their harness models.

### Questions we will answer
1. How do array utilization and throughput differ between prefill and decode, and how much does batching help?
2. For a fixed number of PEs, is one large array or several smaller ones faster once memory bandwidth and real clock speed are included, and does the answer differ between prefill and decode?
3. How much do INT4 weights speed up decode, and how does KV-cache traffic limit that gain, and the gain from batching, as batch size and context length grow?

GPU comparisons will be reported as a percentage of each platform's peak and as position on the roofline rather than raw GOPS, since the GPU has far more hardware. We count 1 MAC as 2 operations.

### Poster session demo
An animated, cycle-by-cycle heatmap of the PE grid from our Verilator runs: the diagonal wavefront filling the array during prefill next to decode lighting up a single row. Alongside it: utilization breakdowns, rooflines, the scale-up vs. scale-out results, and decode throughput vs. batch size for INT8 and INT4 weights at several context lengths.

## PLATFORM CHOICE
- **SystemVerilog + Verilator:** direct control over the hardware's parallel structure and exact cycle counts, without depending on a physical board.
- **Vivado synthesis:** grounds the scale-up vs. scale-out comparison in real clock speeds and resource limits rather than ideal simulator assumptions.
- **NVIDIA GPU + cuBLAS:** a production-grade baseline for the same INT8 matrix workloads.

## SCHEDULE

| Week | Raj (hardware) | Megha (software) |
|---|---|---|
| **Nov 5–8** | PE module and input skew logic; choose target FPGA part | C++ golden model and Verilator harness skeleton; choose LLM shapes |
| **Nov 9** | **Proposal due** | **Proposal due** |
| **Nov 9–15** | Parameterized N×N array with result drain; bit-exact tests | Host tiling driver; bandwidth-limited memory model |
| **Nov 16–22** | PE-activity counters; first synthesis runs | First prefill vs. decode utilization sweeps (8×8 to 32×32); cuBLAS baseline |
| **Nov 23** | **Milestone due** (with first utilization results) | **Milestone due** |
| **Nov 23–29** *(Thanksgiving)* | Multi-array top level (4×16×16, 16×8×8); start place-and-route runs | Analytical model; validate against simulation; add KV-cache attention shapes |
| **Nov 30–Dec 6** | Finish Fmax and resource sweep; load/compute overlap and INT4 weights in the harness | Bandwidth, batching, INT4, and KV-cache sweeps; combine cycle counts with Fmax for effective throughput |
| **Dec 7–10** | Final plots and PE heatmap demo; report (joint) | Final analysis; report (joint) |
| **Dec 10** | **Final report due** | **Final report due** |
| **Dec 11** | **Poster session** | **Poster session** |
