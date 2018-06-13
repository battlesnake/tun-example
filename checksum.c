#include "checksum.h"

uint32_t calc_checksum(const void *buf, size_t len)
{
	const char *begin = buf;
	const char *end = buf + len;
	uint32_t cs = 0xaaaaaaaaUL;
	for (const char *it = begin; it < end; ++it) {
		cs = ~cs << 5 | cs >> 27;
		cs ^= *it;
		if (((it - begin) & 7) == 0) {
			cs = ~cs << 2 | cs >> 30;
		}
	}
	return cs;
}
