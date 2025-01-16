#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "crc.h"
#include "image.h"
#include "rsa_verify.h"

extern char *optarg;

#define IMAGE_OPEN_ERR			-2
#define IMAGE_SIZE_ERR			-3
#define IMAGE_MAP_ERR			-4
#define IMAGE_FORMAT_ERR		-5
#define IMAGE_NAME_ERR			-6
#define IMAGE_HEADER_CRC_ERR	-7
#define IMAGE_DATA_CRCR_ERR		-8
#define IMAGE_RSA_VERIFY_ERR	-9

#define MD5_FORMAT_ERROR		-10
#define MD5_HEADER_CRC_ERROR	-11
#define MD5_FIRMWARE_LEN_ERR	-12
#define MD5_FIRMWARE_MD5_ERR	-13

static int image_check_step = 0x10000;	/* 64K */

static void usage(const char *programname)
{
	fprintf(stderr, "Usage: %s [options]\n"
					"Options:\n"
					" -b <board_name>:	board name\n"
					" -o <bootloader size>:	bootloader size\n"
					" -r :				only check rsa\n"
					" -f <file_name>:	image file need to check\n"
					"\n", programname);
	exit(-1);
}

static int get_uimage_offset(char *image, const off_t size, uint32_t *offset, uint32_t skip_size)
{
	if (image) {
		char *p = image;
		image_header_t *img_header;
		uint32_t off = skip_size;

		while (off < size) {
			img_header = (image_header_t *)(p + off);
			if (UIMAGE_HEADER_MAGIC == ntohl(img_header->ih_magic)) {
				*offset = off;
				return 0;
			}

			off += image_check_step;
		}
	}

	return -1;
}

int main (int argc, char **argv)
{
	char *ptr = NULL;
	int fd = 0;
	uint32_t offset = 0;
	uint32_t hcrc = 0;
	uint32_t dcrc = 0;
	uint32_t checksum = 0;
	struct stat stat;
	int op;
	char *board_name = NULL;
	char *image_file = NULL;
	int ret = -1;
	uint32_t bootloader_size = 0;
	int only_check_rsa = 0;
	uint8_t *sig;
	uint32_t sig_len = 256;
	image_header_t img_header;

	while ((op = getopt(argc, argv, "b:o:f:r")) != -1) {
		switch (op) {
		case 'b':
			if (optarg[0] == '0' && optarg[1] == 0) {
				fprintf(stderr, "wt: Invalid board name.\n");
				usage(argv[0]);
			}
			board_name = optarg;
			break;

		case 'o':
			if (optarg[0] == '0' && optarg[1] == 0) {
				fprintf(stderr, "wt: Invalid bootloader size.\n");
				usage(argv[0]);
			}

			bootloader_size = strtoul(optarg, NULL, 16);
			if (ULONG_MAX == bootloader_size) {
				fprintf(stderr, "wt: Invalid bootloader size(%s).\n", optarg);
				usage(argv[0]);
			}
			break;

		case 'r':
			only_check_rsa = 1;
			break;

		case 'f':
			if (optarg[0] == '0' && optarg[1] == 0) {
				fprintf(stderr, "wt: Invalid image file name.\n");
				usage(argv[0]);
			}
			image_file = optarg;
			break;
		}
	}

	if (!board_name || !image_file) {
		usage(argv[0]);
	}

	if ((fd = open(image_file, O_RDONLY, 0)) < 0) {
		fprintf(stderr, "wt: open %s file error (%s)\n", image_file, strerror(errno));
		return IMAGE_OPEN_ERR;
	}

	fstat(fd, &stat);

	if (stat.st_size <= bootloader_size) {
		fprintf(stderr, "wt: file size(0x%x) is small\n", (unsigned int)stat.st_size);
		ret = IMAGE_SIZE_ERR;
		goto err_mmap;
	}

	ptr = mmap(NULL, stat.st_size, PROT_READ, MAP_DENYWRITE | MAP_PRIVATE, fd, 0);
	if (ptr == (void *)-1) {
		fprintf(stderr, "wt: map %s file error (%s)\n", image_file, strerror(errno));
		ret = IMAGE_MAP_ERR;
		goto err_mmap;
	}

	if (only_check_rsa) {
		char file_board_name[16];
		uint8_t *b_tmp;
		uint8_t *b_firm;

		/*
		 * file + board_name(16) + signatrue(256) + pad(0x10000)
		 */
		b_tmp = (uint8_t *)ptr;
		memset(file_board_name, 0, sizeof(file_board_name));
		memcpy(file_board_name, b_tmp, sizeof(file_board_name) - 1);
		if (strncmp(file_board_name, board_name, sizeof(file_board_name))) {
			ret = -1;
			fprintf(stderr, "wt: board name failed(%s/%s)\n", file_board_name, board_name);
			goto check_out;
		}

		sig = (uint8_t *)ptr + 16;
		b_firm = (uint8_t *)(ptr) + 0x10000;

		ret = rsa_verify(NULL, 0, b_firm, stat.st_size - 0x10000, sig, sig_len);

		if (ret != 0) {
			fprintf(stderr, "wt: rsa verify error(%s 0x%x 0x%x)\n", image_file, bootloader_size, offset);
			ret = IMAGE_RSA_VERIFY_ERR;
			goto err_out;
		} else {
			ret = 0;
			goto check_out;
		}
	}

	// skip bootloader
	offset = bootloader_size;
	ret = get_uimage_offset(ptr, stat.st_size, &offset, bootloader_size);
	if (ret < 0) {
		fprintf(stderr, "wt: firmware format error\n");
		ret = IMAGE_FORMAT_ERR;
		goto err_mmap;
	}

	memmove(&img_header, ptr + offset, sizeof(image_header_t));
	if (UIMAGE_HEADER_MAGIC != ntohl(img_header.ih_magic)) {
		fprintf(stderr, "wt: firmware format error(0x%x)\n", offset);
		ret = IMAGE_FORMAT_ERR;
		goto err_out;
	}

	if (strlen(board_name) != strlen((const char *)(img_header.ih_name))) {
		fprintf(stderr, "wt: firmware name len error(%s %s)\n", board_name, img_header.ih_name);
		ret = IMAGE_NAME_ERR;
		goto err_out;
	}

	if (memcmp(board_name, img_header.ih_name, strlen(board_name))) {
		fprintf(stderr, "wt: firmware name error(%s %s)\n", board_name, img_header.ih_name);
		ret = IMAGE_NAME_ERR;
		goto err_out;
	}

	checksum = ntohl(img_header.ih_hcrc);
	img_header.ih_hcrc = 0;
	hcrc = get_crc32(0, (uint8_t *)(&img_header), sizeof(image_header_t));
	if (hcrc != checksum) {
		fprintf(stderr, "wt: firmware header crc error(%s %s)\n", board_name, img_header.ih_name);
		ret = IMAGE_HEADER_CRC_ERR;
		goto err_out;
	}

	checksum = ntohl(img_header.ih_dcrc);
	dcrc = get_crc32(0, (uint8_t *)(ptr + offset + sizeof(image_header_t)), ntohl(img_header.ih_size));
	if (dcrc != checksum) {
		fprintf(stderr, "wt: firmware data crc error(%s %s)\n", board_name, img_header.ih_name);
		ret = IMAGE_HEADER_CRC_ERR;
		goto err_out;
	}

	sig = (uint8_t *)ptr + bootloader_size;

	ret = rsa_verify((uint8_t *)(ptr), bootloader_size, (uint8_t *)(ptr + offset), stat.st_size - offset, sig, sig_len);
	if (ret != 0) {
		fprintf(stderr, "wt: verify error(%s 0x%x 0x%x)\n", image_file, bootloader_size, offset);
		ret = IMAGE_RSA_VERIFY_ERR;
		goto err_out;
	}

check_out:
	munmap(ptr, stat.st_size);
	close(fd);

	return ret;

err_out:
	munmap(ptr, stat.st_size);

err_mmap:
	close(fd);

	return ret;
}
