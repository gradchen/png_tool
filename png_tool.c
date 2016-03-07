#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "zlib.h"

//#define DEBUG_PRINT
#define DUMP_PRINT

#define PNG_MAX_FILE_SIZE	(512 * 1024)
unsigned char buffer[PNG_MAX_FILE_SIZE];
unsigned char __inflate_buffer[PNG_MAX_FILE_SIZE];
unsigned char *inflate_buffer = __inflate_buffer + 512;
unsigned char recon_buffer[PNG_MAX_FILE_SIZE];
int png_width, png_height;

int parse_png_header(int size, int offset)
{
	unsigned char *ptr = &buffer[offset];

	if (offset >= size) {
		return 0;
	}

	if (	(*ptr == 0x89) && 
			(*(ptr+1) == 'P') && 
			(*(ptr+2) == 'N') &&
			(*(ptr+3) == 'G')) {
		;
	} else {
		printf("ERROR: it's not PNG file!!!\n");
		return -1;
	}

#if defined(DEBUG_PRINT)
	printf(".PNG\n");
#endif
	return (offset + 8);
}

void dump_IHDR(unsigned char *ptr, int size)
{
	png_width = *(ptr) << 24 | *(ptr+1) << 16 | *(ptr+2) << 8 | *(ptr+3);
	ptr += 4;
	png_height = *(ptr) << 24 | *(ptr+1) << 16 | *(ptr+2) << 8 | *(ptr+3);

#if defined(DUMP_PRINT)
	printf("PNG SIZE = [%d x %d]\n\n", png_width, png_height);
	printf("static u8 ubnt_logo[%d] =\n", png_width * png_height);
#endif
}

void recon_from_inflate_buffer()
{
	unsigned char filter_type;
	unsigned char a, b, c, x;
	unsigned char pr;
	int p, pa, pb, pc;
	int i, j;

	for (i = 0; i < png_height; i++) {
		for (j = 0; j < (png_width + 1); j++) {
			if (0 == j) {
				filter_type = inflate_buffer[i * (png_width + 1) + j];
			} else {
				/*
				 * Calculate:
				 *    c b
				 *    a x
				 */
				x = inflate_buffer[i * (png_width + 1) + j];
				a = inflate_buffer[i * (png_width + 1) + j - 1];
				b = inflate_buffer[(i -1) * (png_width + 1) + j];
				c = inflate_buffer[(i -1) * (png_width + 1) + j - 1];
				/* for first low */
				if (0 == i) {
					c = 0;
					b = 0;
				}
				/* for first column */
				if (1 == j) {
					c = 0;
					a = 0;
				}

				switch (filter_type) {
				case 0x00:
					inflate_buffer[i * (png_width + 1) + j] = x;
					break;
				case 0x01:
					inflate_buffer[i * (png_width + 1) + j] = x + a;
					break;
				case 0x02:
					inflate_buffer[i * (png_width + 1) + j] = x + b;
					break;
				case 0x03:
					inflate_buffer[i * (png_width + 1) + j] = x + (a + b) / 2;
					break;
				case 0x04:
					p = a + b - c;
					pa = abs(p - a);
					pb = abs(p - b);
					pc = abs(p - c);
					if ((pa <= pb) && (pa <= pc)) {
						pr = a;
					} else if (pb <= pc) {
						pr = b;
					} else {
						pr = c;
					}
					inflate_buffer[i * (png_width + 1) + j] = x + pr;
					break;
				}
			}
		}
	}
}

void dump_scanlines()
{
	int i, j;
	unsigned char x;

	printf("{");
	for (i = 0; i < png_height; i++) {
		for (j = 0; j < (png_width + 1); j++) {
			if (0 == j) {
				printf("\n");
			} else {
				x = inflate_buffer[i * (png_width + 1) + j];
				printf("0x%02x,", x);
				//printf("%02x", x);
			}
		}
	}
	printf("\n};\n");
}

void dump_IDAT(unsigned char *ptr, int size)
{
	int i;

#if defined(DEBUG_PRINT)
	/* zlib-flate -uncompress < a.gz > a.txt */
	int fd;
	if ((fd = open("a.gz", O_RDWR | O_CREAT, 0660)) < 0) {
		perror("a.gz");
		return;
	}
	if (write(fd, ptr, size) != size) {
		perror("a.gz");
	}
	close(fd);
#endif

	/* inflate IDAT chunk */
	z_stream inflate_stream;
	inflate_stream.zalloc = Z_NULL;
	inflate_stream.zfree = Z_NULL;
	inflate_stream.opaque = Z_NULL;

	inflate_stream.avail_in = (uInt)size;
	inflate_stream.next_in = (Bytef *)ptr;
	inflate_stream.avail_out = (uInt)PNG_MAX_FILE_SIZE;
	inflate_stream.next_out = (Bytef *)inflate_buffer;

	/* the actual DE-compression work */
	inflateInit(&inflate_stream);
	inflate(&inflate_stream, Z_NO_FLUSH);
	inflateEnd(&inflate_stream);

	recon_from_inflate_buffer();

#if defined(DUMP_PRINT)
	dump_scanlines();
#endif
}

int parse_fourcc(int size, int offset)
{
	unsigned char *ptr = &buffer[offset];
	int fourcc_len;

	if (offset >= size) {
		return 0;
	}

	fourcc_len = *(ptr) << 24 | *(ptr+1) << 16 | *(ptr+2) << 8 | *(ptr+3);
#if defined(DEBUG_PRINT)
	printf("fourcc_len = %d\n", fourcc_len);
	printf("fourcc: %c%c%c%c\n", *(ptr+4), *(ptr+5), *(ptr+6), *(ptr+7));
#endif

	if (	(*(ptr+4) == 'I') && 
			(*(ptr+5) == 'H') && 
			(*(ptr+6) == 'D') &&
			(*(ptr+7) == 'R')) {
		dump_IHDR(ptr + 8, fourcc_len);
	}

	if (	(*(ptr+4) == 'I') && 
			(*(ptr+5) == 'D') && 
			(*(ptr+6) == 'A') &&
			(*(ptr+7) == 'T')) {
		dump_IDAT(ptr + 8, fourcc_len);
	}

	/* TODO: if CRC error, then return -1 */
	
	return (offset + fourcc_len + 12); /* Length, Chunk type, CRC = 4 + 4 + 4 */
}

int parse_png(int size, int offset)
{
	int rval;
	unsigned char *ptr = &buffer[offset];

	if (offset >= size) {
		return 0;
	}

	rval = parse_png_header(size, offset);
	if (rval <= 0) {
		return rval;
	}

	while (1) {
		offset = rval;
		rval = parse_fourcc(size, offset);
		if (rval <= 0) {
			break;
		}
	}

	return rval;
}

int main(int argc, char **argv)
{
	const char *filename;
	int fd;
	struct stat stat;

	if (argc < 2) {
		printf("\nUsage: %s <PNG file>\n\n", argv[0]);
		return -1;
	}

	filename = argv[1];
	if ((fd = open(filename, O_RDONLY, 0)) < 0) {
		perror(filename);
		return -1;
	}

	if (fstat(fd, &stat) < 0) {
		perror(filename);
		close(fd);
		return -1;
	}

	if (read(fd, buffer, stat.st_size) != stat.st_size) {
		perror(filename);
		close(fd);
		return -1;
	}

	if (parse_png(stat.st_size, 0) < 0) {
		printf("\n\nERROR: PNG parsing failed!!!\n\n");
	}

	close(fd);
	return 0;
}
