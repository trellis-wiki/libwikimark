# libwikimark benchmark harness

Minimal benchmark suite to detect performance regressions. Not a
comparative study against other Markdown parsers — that work belongs
in the Trellis engine's benchmark suite.

## Running

```bash
mkdir -p build && cd build
cmake -DWIKIMARK_BENCH=ON ..
make bench
```

Or run a single input manually:

```bash
WM_BENCH_TEMPLATE_DIR=../test_data/templates \
WM_BENCH_PAGES_DIR=../test_data/pages \
./bench_wikimark ../bench/inputs/kitchen-sink.wm 1000
```

## Inputs

| File | Size | Purpose |
|------|------|---------|
| `plain.md` | ~2.6 KB | Pure GFM, no WikiMark features. Baseline for the cmark-gfm pipeline with libwikimark extensions registered but not triggered. |
| `wiki-heavy.wm` | ~4.1 KB | Dense wiki-link content simulating an encyclopedia-style page. Measures wiki-link scanner cost. |
| `kitchen-sink.wm` | ~2.5 KB | Every feature: frontmatter, variables, templates, embeds, attributes, annotations, callouts. Pessimistic case. |

## Output format

Each run prints one JSON line:

```json
{"input":"...", "iterations":1000, "input_bytes":2623,
 "median_s":0.000125, "p95_s":0.000151, "p99_s":0.000207,
 "min_s":0.000118, "max_s":0.000285, "total_s":0.133,
 "throughput_mbps":18.85, "peak_rss_kb":1984,
 "avg_output_bytes":3402}
```

Machine-readable on purpose — future regression scripts can diff
two runs without parsing human-oriented tables.

## Methodology

- 10 warm-up iterations (or `iterations/5` for small runs) are
  discarded before timing.
- `clock_gettime(CLOCK_MONOTONIC)` for wall-clock.
- `getrusage(RUSAGE_SELF).ru_maxrss` for peak RSS (reported in KB on
  Linux, converted from bytes on macOS).
- Each render allocates and frees a full HTML output; allocator cost
  is included.
- `CMARK_OPT_UNSAFE` is set so raw HTML from template callbacks is
  preserved in the output (otherwise kitchen-sink numbers understate
  what real rendering does).

## Interpretation

Numbers are sensitive to CPU, clock scaling, and concurrent load —
use them for trend detection on a single machine, not as absolute
claims. CI runners are particularly noisy; regressions under 15%
should not be treated as signal.
