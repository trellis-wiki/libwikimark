/**
 * Minimal benchmark harness for libwikimark.
 *
 * Reads one input file, renders it N times (default 1000), reports
 * median / p95 / p99 wall-clock per iteration, total throughput in
 * MB/s, and peak RSS.
 *
 * Usage:
 *   bench_wikimark <input-file> [iterations]
 *
 * Output is deliberately machine-readable (JSON on one line per run)
 * so a regression script can compare against a baseline.
 */

#include "wikimark.h"
#include "test_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "bench: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (end < 0) { fclose(f); return NULL; }

    char *buf = (char *)malloc((size_t)end + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)end, f);
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static long peak_rss_kb(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
    /* Linux reports in kilobytes; macOS reports in bytes. */
#ifdef __APPLE__
    return (long)(ru.ru_maxrss / 1024);
#else
    return (long)ru.ru_maxrss;
#endif
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s <input-file> [iterations]\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    int iterations = (argc >= 3) ? atoi(argv[2]) : 1000;
    if (iterations < 1) iterations = 1;

    size_t input_len = 0;
    char *input = read_all(path, &input_len);
    if (!input) return 2;

    /* Minimal engine context so templates and embeds in the kitchen-sink
     * input render against the libwikimark fixture data. The bench
     * input files were written to work with these fixtures. */
    const char *template_dir = getenv("WM_BENCH_TEMPLATE_DIR");
    const char *pages_dir    = getenv("WM_BENCH_PAGES_DIR");
    test_engine *engine = test_engine_new(template_dir, pages_dir);
    wikimark_context ctx = test_engine_get_context(engine);
    wikimark_config cfg = wikimark_config_default();

    /* Warm-up: discard first 10 iterations from the timing to
     * amortize first-run caches. */
    int warmup = iterations < 50 ? iterations / 5 : 10;
    for (int i = 0; i < warmup; i++) {
        char *html = wikimark_render(input, input_len, CMARK_OPT_UNSAFE, &cfg, &ctx);
        if (html) wikimark_free(html);
    }

    double *samples = (double *)malloc((size_t)iterations * sizeof(double));
    if (!samples) { free(input); test_engine_free(engine); return 2; }

    double t_total_start = now_seconds();
    size_t total_output_bytes = 0;
    for (int i = 0; i < iterations; i++) {
        double t0 = now_seconds();
        char *html = wikimark_render(input, input_len, CMARK_OPT_UNSAFE, &cfg, &ctx);
        double t1 = now_seconds();
        samples[i] = t1 - t0;
        if (html) {
            total_output_bytes += strlen(html);
            wikimark_free(html);
        }
    }
    double t_total = now_seconds() - t_total_start;

    qsort(samples, iterations, sizeof(double), cmp_double);
    double median = samples[iterations / 2];
    double p95    = samples[(int)((double)iterations * 0.95)];
    double p99    = samples[(int)((double)iterations * 0.99)];
    double min    = samples[0];
    double max    = samples[iterations - 1];

    double throughput_mbps =
        ((double)input_len * (double)iterations / (1024.0 * 1024.0)) / t_total;

    long rss_kb = peak_rss_kb();

    printf(
        "{\"input\":\"%s\","
        "\"iterations\":%d,"
        "\"input_bytes\":%zu,"
        "\"median_s\":%.9f,"
        "\"p95_s\":%.9f,"
        "\"p99_s\":%.9f,"
        "\"min_s\":%.9f,"
        "\"max_s\":%.9f,"
        "\"total_s\":%.9f,"
        "\"throughput_mbps\":%.3f,"
        "\"peak_rss_kb\":%ld,"
        "\"avg_output_bytes\":%.0f}\n",
        path, iterations, input_len,
        median, p95, p99, min, max, t_total,
        throughput_mbps, rss_kb,
        (double)total_output_bytes / (double)iterations
    );

    free(samples);
    free(input);
    test_engine_free(engine);
    return 0;
}
