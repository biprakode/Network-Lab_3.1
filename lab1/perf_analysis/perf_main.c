#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "perf_analyzer.h"

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	srand(time(NULL));

	scheme_t schemes[] = {
		SCHEME_CHECKSUM16,
		SCHEME_CRC8,
		SCHEME_CRC10,
		SCHEME_CRC16,
		SCHEME_CRC32
	};
	int num_schemes = sizeof(schemes) / sizeof(schemes[0]);

	perf_result_t results[5];

	printf("Performance Analysis: Stress Testing Checksum & CRC Schemes\n");
	printf("Running 10-second burst per scheme...\n\n");

	for (int i = 0; i < num_schemes; i++) {
		printf("[%d/%d] Testing %s... ", i + 1, num_schemes,
		       (schemes[i] == SCHEME_CHECKSUM16) ? "checksum" :
		       (schemes[i] == SCHEME_CRC8) ? "crc8" :
		       (schemes[i] == SCHEME_CRC10) ? "crc10" :
		       (schemes[i] == SCHEME_CRC16) ? "crc16" :
		       (schemes[i] == SCHEME_CRC32) ? "crc32" : "unknown");
		fflush(stdout);

		results[i] = perf_analyze_scheme(schemes[i], 10.0);

		printf("done (%.0f Mbps)\n", results[i].throughput_bits_per_sec / 1e6);
	}

	perf_print_results(results, num_schemes);
	perf_write_csv("perf_results.csv", results, num_schemes);

	return 0;
}
