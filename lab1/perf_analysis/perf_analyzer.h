#ifndef PERF_ANALYZER_H
#define PERF_ANALYZER_H

#include <stdint.h>
#include <stddef.h>
#include "../scheme.h"

typedef struct {
	scheme_t scheme;
	const char *scheme_name;
	uint64_t frames_processed;
	uint64_t bytes_processed;
	double elapsed_seconds;
	double throughput_bytes_per_sec;
	double throughput_bits_per_sec;
	double throughput_frames_per_sec;
} perf_result_t;

perf_result_t perf_analyze_scheme(scheme_t scheme, double duration_seconds);

void perf_print_results(perf_result_t *results, int count);

void perf_write_csv(const char *filename, perf_result_t *results, int count);

#endif
