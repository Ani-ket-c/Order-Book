# Order Book

A C++20 limit order book and market simulation benchmark focused on terminal latency results.

The project implements an in-memory matching engine with:

- limit order add, cancel, and market order matching
- lock-free SPSC gateway queue
- cycle-based latency profiling with percentile histograms
- microstructure metrics such as mid price, spread, OFI, VWAP, and Kyle's lambda
- terminal-only benchmark output

## Latest Benchmark

The latest run measured more than 5 million in-memory order events:

```text
Kind              p50        p95        p99      p99.9     p99.99        max       mean      samples
---------- ---------- ---------- ---------- ---------- ---------- ---------- ---------- ------------
Add             255ns      672ns      2.0us      8.2us     65.5us     55.52ms      358ns      3127217
Cancel          375ns      791ns      1.3us      2.0us     16.4us    296.8us      393ns      1563428
Match           190ns      2.0us      4.1us      8.2us     16.4us    125.0us      423ns       311901
EndToEnd         1.07s      1.07s      1.07s      1.07s      1.07s    664.51ms   467.20ms     5002546

Performance Results
Metric                  Current Run
Latency per order       ~373 ns
Throughput              ~2.7M TPS
Orders measured         5002546
Benchmarks were run with 5002546 in-memory orders (no file I/O during measurement).
```

## Build

On Windows with Visual Studio 2022 and CMake:

```powershell
cd D:\hft2\Limit-Order-Book
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Or with CMake directly:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_PROFILING=ON
cmake --build build --config Release -j 4
```

## Run

Quick run:

```powershell
.\build\Release\LimitOrderBook.exe 1000
```

Around 5 million order events:

```powershell
.\build\Release\LimitOrderBook.exe 62500
```

Using the build script:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Run -Steps 62500
```

## Output

The program prints the latency histogram and performance summary directly in the terminal.

It also writes:

- `simulation_results.csv`
- `latency_report.txt`

These generated files are benchmark artifacts and are not required for source builds.

## Project Structure

```text
Analytics/           Microstructure analytics
Gateway/             Lock-free SPSC queue and gateway tests
Generate_Orders/     Order generation helpers
Limit_Order_Book/    Core book, limit, order, and object pool
Process_Orders/      Order pipeline
Profiling/           Latency profiler and profiler smoke test
Protocol/            Binary order message structures
Simulation/          Market simulator
main.cpp             Terminal benchmark entry point
```

## Notes

The benchmark focuses on in-memory order processing. The core add/cancel/match operation means are sub-microsecond in the latest run. End-to-end latency includes gateway queueing and simulation effects, so it should be interpreted separately from the core matching-engine latency.
