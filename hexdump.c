#include <stdio.h>
#include <stddef.h>

#define STEP 16

void hexdump(const char *title, const void *p, size_t len)
{
	const char *cp = p;
	printf("%s\n", title);
	for (size_t i = 0; i < len ; i += STEP) {
		printf("%04zx |", i);
		for (size_t j = 0; j < STEP; j++) {
			size_t k = i + j;
			if (j % 4 == 0) {
				printf(" ");
			}
			if (k < len) {
				printf(" %02hhx", cp[k]);
			} else {
				printf("   ");
			}
		}
		printf(" |");
		for (size_t j = 0; j < STEP; j++) {
			size_t k = i + j;
			char c = k < len ? cp[k] : ' ';
			c = c <= 32 ? '.' : c;
			if (j % 4 == 0) {
				printf(" ");
			}
			printf("%c", c);
		}
		printf("\n");
	}
	printf("\n");
}
