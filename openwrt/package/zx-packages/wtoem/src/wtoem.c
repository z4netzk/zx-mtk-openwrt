#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/des.h>
#include <openssl/md5.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "wtoem.h"

#define __packed __attribute__((packed))

#define WTINFO_MTD_PART_NAME		"woem"
#define WTINFO_MAGIC_LEN			4
#define WTINFO_TOTAL_LEN	0x10000

#define WTINFO_MAGIC	"WTUP"

#define WTINFO_RES_LEN                 (WTINFO_TOTAL_LEN - 1 - 4)

typedef struct _wtinfo {
	unsigned char magic[WTINFO_MAGIC_LEN];
	unsigned char part;
	unsigned char res[WTINFO_RES_LEN];
} __packed WTINFO;

static int _wtinfo_mtd_open(void)
{
	FILE *fp;
	int i, part_size;
	char dev[PATH_MAX];
	char *path = NULL;
	struct stat s;
	int fd = -1;

	if ((fp = fopen("/proc/mtd", "r")))
	{
		while( fgets(dev, sizeof(dev), fp) )
		{
			if( strstr(dev, WTINFO_MTD_PART_NAME) && sscanf(dev, "mtd%d: %08x", &i, &part_size) )
			{
				sprintf(dev, "/dev/mtdblock%d", i);
				if( stat(dev, &s) > -1 && (s.st_mode & S_IFBLK) )
				{
					fd = open(dev, O_RDWR);
				}
			}
		}
		fclose(fp);
	}

	return fd;
}

static int _wtinfo_read(WTINFO *info)
{
	int fd = -1;
	int ret = -1;
	char *mmap_area = MAP_FAILED;

	fd = _wtinfo_mtd_open();

	if(fd > -1) {
		mmap_area = (char *) mmap(
			NULL, WTINFO_TOTAL_LEN, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);

		if(mmap_area != MAP_FAILED) {
			if (info) {
				memcpy(info, mmap_area, WTINFO_TOTAL_LEN);
				ret = 0;
			}
		} else {
			syslog(LOG_ERR, "mmap failed\n");
		}
	}

	if (fd > 0)
		close(fd);

	if(mmap_area != MAP_FAILED) {
		munmap(mmap_area, WTINFO_TOTAL_LEN);
	}

	return ret;
}

int wtoem_reverse(void)
{
	WTINFO *l_info = NULL;
	int ret = -1;
	unsigned char part = 1;

	l_info =(WTINFO *)malloc(sizeof(WTINFO));

	if (l_info) {
		memset(l_info, 0, sizeof(WTINFO));
		ret = _wtinfo_read(l_info);

		if (0 == ret) {
			if ((l_info->magic[0] == 'W') &&
				(l_info->magic[1] == 'T') &&
				(l_info->magic[2] == 'U') &&
				(l_info->magic[3] == 'P')) {
				if (l_info->part == 0) {
					part = 1;
				} else {
					part = 0;
				}
			} else {
				part = 1;
			}
		} else {
			memcpy(l_info->magic, WTINFO_MAGIC, sizeof(l_info->magic));
		}

		wtoem_save(part);

		free(l_info);
	}

	return 0;
}

int wtoem_save(unsigned char part)
{
	int fd = -1;
	char *mtd = NULL;
	int ret = -1;
	WTINFO *l_info;
	char *mmap_area = MAP_FAILED;

	l_info =(WTINFO *)malloc(sizeof(WTINFO));

	if (!l_info) {
		syslog(LOG_ERR, "malloc wtinfo failed!");

		return ret;
	}

	memset(l_info, 0, sizeof(WTINFO));

	memcpy(l_info->magic, WTINFO_MAGIC, sizeof(l_info->magic));
	l_info->part = part;

	fd = _wtinfo_mtd_open();

	if(fd > -1) {
		mmap_area = (char *) mmap(
			NULL, WTINFO_TOTAL_LEN, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);

		if(mmap_area != MAP_FAILED) {
			memcpy(mmap_area, l_info, WTINFO_TOTAL_LEN);

			/* Write out */
			msync(mmap_area, WTINFO_TOTAL_LEN, MS_SYNC);
			fsync(fd);
			ret = 0;
		} else {
			syslog(LOG_ERR, "mmap failed\n");
		}
	} else {
		printf("open error\n");
	}

	if (fd > 0)
		close(fd);

	if (mmap_area != MAP_FAILED) {
		munmap(mmap_area, WTINFO_TOTAL_LEN);
	}

	free(l_info);

	return ret;
}
