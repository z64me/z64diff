//
// z64diff <z64me>
//
// a simple utility for finding out what
// has moved or changed inside a romhack
//

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// https://opensource.apple.com/source/Libc/Libc-825.40.1/string/FreeBSD/memmem.c.auto.html
void *
memmem(const void *l, size_t l_len, const void *s, size_t s_len)
{
	register char *cur, *last;
	const char *cl = (const char *)l;
	const char *cs = (const char *)s;

	/* we need something to compare */
	if (l_len == 0 || s_len == 0)
		return NULL;

	/* "s" must be smaller or equal to "l" */
	if (l_len < s_len)
		return NULL;

	/* special case where s_len == 1 */
	if (s_len == 1)
		return memchr(l, (int)*cs, l_len);

	/* the last position where its possible to find "s" in "l" */
	last = (char *)cl + l_len - s_len;

	for (cur = (char *)cl; cur <= last; cur++)
		if (cur[0] == cs[0] && memcmp(cur, cs, s_len) == 0)
			return cur;

	return NULL;
}

/* minimal file loader
 * returns 0 on failure
 * returns pointer to loaded file on success
 */
void *loadfile(const char *fn, size_t *sz)
{
	FILE *fp;
	void *dat;
	
	/* rudimentary error checking returns 0 on any error */
	if (
		!fn
		|| !sz
		|| !(fp = fopen(fn, "rb"))
		|| fseek(fp, 0, SEEK_END)
		|| !(*sz = ftell(fp))
		|| fseek(fp, 0, SEEK_SET)
		|| !(dat = malloc(*sz))
		|| fread(dat, 1, *sz, fp) != *sz
		|| fclose(fp)
	)
		return 0;
	
	return dat;
}

// Returns pointer to dmadata, if found
const void *getdma(const void *f, size_t sz)
{
	const uint8_t magic[20] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x60, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x60
	};
	
	return memmem(f, sz, magic, sizeof(magic));
}

// Returns byte offset of block within body
size_t offwithin(const void *body, const void *block)
{
	const uint8_t *hay = body;
	const uint8_t *needle = block;
	
	return (size_t)(needle - hay);
}

// Returns a BigEndian 32-bit word
uint32_t readBEu32(const void *d)
{
	const uint8_t *b = d;
	
	return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

int main(int argc, char *argv[])
{
	char *fnA = argv[1];
	char *fnB = argv[2];
	uint8_t *fA;
	uint8_t *fB;
	const uint8_t *dmaA;
	const uint8_t *dmaB;
	size_t fszA;
	size_t fszB;
	uint32_t dmaoffA;
	uint32_t dmaoffB;
	uint32_t dmasz;
	uint32_t i;
	bool hasChanged = false;
	
	// Show arguments
	if (argc != 3)
	{
		fprintf(stderr, "args: z64diff old.z64 new.z64\n");
		return EXIT_FAILURE;
	}
	
	// Load files
	if (!(fA = loadfile(fnA, &fszA)))
		fprintf(stderr, "failed to load '%s'\n", fnA);
	if (!(fB = loadfile(fnB, &fszB)))
		fprintf(stderr, "failed to load '%s'\n", fnB);
	if (!fA || !fB)
		return EXIT_FAILURE;
	
	// Size assert
	if (fszA != fszB)
	{
		fprintf(stderr, "files are of different sizes\n");
		return EXIT_FAILURE;
	}
	
	// Get dmadata
	if (!(dmaA = getdma(fA, fszA)))
		fprintf(stderr, "failed to find dmadata in file '%s'\n", fnA);
	if (!(dmaB = getdma(fB, fszB)))
		fprintf(stderr, "failed to find dmadata in file '%s'\n", fnB);
	if (!dmaA || !dmaB)
		return EXIT_FAILURE;
	
	// Assert dmadata is at the same address in each
	if ((dmaoffA = offwithin(fA, dmaA)) != (dmaoffB = offwithin(fB, dmaB)))
	{
		fprintf(stderr, "dmadata at different addresses in each file...\n");
		fprintf(stderr, " -> %08x   %s\n", dmaoffA, fnA);
		fprintf(stderr, " -> %08x   %s\n", dmaoffB, fnB);
		return EXIT_FAILURE;
	}
	fprintf(stderr, "dmadata lives at %08x\n", dmaoffA);
	
	// Locate dma index describing dma table
	for (i = dmaoffA, dmasz = 0; i < fszA - 16; i += 16)
	{
		if (readBEu32(fA + i) == dmaoffA)
		{
			if (memcmp(fA + i, fB + i, 16))
			{
				fprintf(stderr, "dmadata length mismatch!\n");
				return EXIT_FAILURE;
			}
			dmasz = readBEu32(fA + i + 4) - dmaoffA;
			break;
		}
	}
	if (!dmasz)
	{
		fprintf(stderr, "failed to locate dmadata size in file '%s'\n", fnA);
		return EXIT_FAILURE;
	}
	
	// Walk the tables
	for (i = 0; i < dmasz; i += 16)
	{
		uint32_t startA = readBEu32(dmaA + i + 8);
		uint32_t startB = readBEu32(dmaB + i + 8);
		uint32_t endA = readBEu32(dmaA + i + 4);
		uint32_t endB = readBEu32(dmaB + i + 4);
		int index = i / 16;
		
		//fprintf(stderr, "%d : %08x vs %08x\n", index, endA - startA, endB - startB);
		//fprintf(stderr, "%d : %08x - %08x\n", index, startA, endA);
		
		if (startA != startB)
		{
			fprintf(stderr, "warning: file %d was relocated\n", index);
			hasChanged = true;
		}
		if (endA != endB)
		{
			fprintf(stderr, "warning: file %d was resized\n", index);
			hasChanged = true;
		}
		if ((endA - startA) == (endB - startB)
			&& memcmp(fA + startA, fB + startB, endA - startA)
		)
		{
			fprintf(stderr, "warning: file %d (%08x - %08x) was modified\n", index, startA, endA);
			hasChanged = true;
		}
	}
	
	// No functional differences
	if (hasChanged == false)
	{
		fprintf(stderr, "no files referenced by dmadata were modified\n");
		if (memcmp(fA, fB, fszA))
			fprintf(stderr, "(there are differences in blocks not referenced by dmadata, though!)\n");
	}
	
	// Cleanup
	free(fA);
	free(fB);
	return EXIT_SUCCESS;
}
