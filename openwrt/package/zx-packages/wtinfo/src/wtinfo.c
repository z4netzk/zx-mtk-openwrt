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

#include "cjson/cJSON.h"

#define __packed __attribute__((packed))

#define WTINFO_JSON_DES_KEY	"wtinfo-des-v1"

#define WTINFO_MTD_PART_NAME		"wtinfo"
#define WTINFO_MAGIC_LEN			6
#define WTINFO_VER_LEN			4
#define WTINFO_MD5_LEN			16
#define WTINFO_MAC_LEN			6

#define WTINFO_TOTAL_LEN		0x10000
#define WTINFO_JSON_LEN			(WTINFO_TOTAL_LEN - WTINFO_MAGIC_LEN - WTINFO_VER_LEN - WTINFO_MD5_LEN - WTINFO_MAC_LEN)

#define WTINFO_MAGIC	"WTINFO"
#define WTINFO_VER		"0001"

typedef struct _wtinfo {
	unsigned char magic[WTINFO_MAGIC_LEN];
	unsigned char ver[WTINFO_VER_LEN];
	unsigned char mac[WTINFO_MAC_LEN];
	unsigned char md5[WTINFO_MD5_LEN];
	unsigned char json_str[WTINFO_JSON_LEN];
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

static void _wtinfo_des_encrypt(const char *str, const char *enc_str, int len, const char *des_key)
{
	DES_key_schedule key_schedule;
	DES_cblock key;
	DES_cblock ivec;

	DES_string_to_key(des_key, &key);
	DES_set_key_checked(&key, &key_schedule);
	memset((char*) &ivec, 0, sizeof(ivec));

	DES_ncbc_encrypt((unsigned char*)str, (unsigned char*)enc_str, len, &key_schedule, &ivec, DES_ENCRYPT);
}

static void _wtinfo_des_decrypt(const char *enc_str, const char *des_str, int len, const char *des_key)
{
	DES_key_schedule key_schedule;
	DES_cblock key;
	DES_cblock ivec;

	DES_string_to_key(des_key, &key);
	DES_set_key_checked(&key, &key_schedule);
	memset((char*) &ivec, 0, sizeof(ivec));

	DES_ncbc_encrypt((unsigned char*)enc_str, (unsigned char*)des_str, len, &key_schedule, &ivec, DES_DECRYPT);
}

const char *wtinfo_get_val(void *json_obj, const char *key)
{
	if (json_obj) {
		cJSON *jvalue;

		jvalue = cJSON_GetObjectItem(json_obj, key);
		if (jvalue) {
			if (jvalue->type == cJSON_String) {
				return jvalue->valuestring;
			}
		}
	}

	return NULL;
}

static int _macstr_to_hex(unsigned char *hex, const unsigned char *str)
{
	int i = 0;
	unsigned char c;
	const unsigned char *p;

	if (NULL == hex || NULL == str)
	{
		goto err;
	}

	p = str;

	for (i = 0; i < 6; i++)
	{
		if (*p == '-' || *p == ':')
		{
			p++;
		}

		if (*p >= '0' && *p <= '9')
		{
			c  = *p++ - '0';
		}
		else if (*p >= 'a' && *p <= 'f')
		{
			c  = *p++ - 'a' + 0xa;
		}
		else if (*p >= 'A' && *p <= 'F')
		{
			c  = *p++ - 'A' + 0xa;
		}
		else
		{
			goto err;
		}

		c <<= 4;	/* high 4bits of a byte */

		if (*p >= '0' && *p <= '9')
		{
			c |= *p++ - '0';
		}
		else if (*p >= 'a' && *p <= 'f')
		{
			c |= (*p++ - 'a' + 0xa);
		}
		else if (*p >= 'A' && *p <= 'F')
		{
			c |= (*p++ - 'A' + 0xa);
		}
		else
		{
			goto err;
		}

		hex[i] = c;
	}

	return 0;

err:
	return -1;
}

int wtinfo_save(const unsigned char *json_str)
{
	int fd = -1;
	char *mtd = NULL;
	int ret = -1;
	WTINFO *l_info;
	cJSON *json_obj = NULL;
	cJSON *jvalue;
	const char *jmac = NULL;
	char *mmap_area = MAP_FAILED;
	unsigned char s_mac[6];

	json_obj = cJSON_Parse(json_str);
	if (json_obj) {
		jvalue = cJSON_GetObjectItem(json_obj, "mac");
		if (jvalue) {
			jmac = jvalue->valuestring;
		}
	} else {
		printf("json format error(%s)\n", json_str);
		return -1;
	}

	if (NULL == jmac) {
		printf("no mac value(%s)\n", json_str);
		cJSON_Delete(json_obj);
		return -1;
	}

	if (0 != _macstr_to_hex(s_mac, jmac)) {
		printf("mac format error(%s)\n", jmac);
		cJSON_Delete(json_obj);
		return -1;
	}

	l_info =(WTINFO *)malloc(sizeof(WTINFO));

	if (!l_info) {
		syslog(LOG_ERR, "malloc wtinfo failed!");

		return ret;
	}

	memset(l_info, 0, sizeof(WTINFO));

	memcpy(l_info->magic, WTINFO_MAGIC, sizeof(l_info->magic));
	memcpy(l_info->ver, WTINFO_VER, sizeof(l_info->ver));
	memcpy(l_info->mac, s_mac, sizeof(l_info->mac));

	fd = _wtinfo_mtd_open();

	if(fd > -1) {
		mmap_area = (char *) mmap(
			NULL, WTINFO_TOTAL_LEN, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);

		if(mmap_area != MAP_FAILED) {
			char md5[WTINFO_MD5_LEN];
			char json_des_str[WTINFO_JSON_LEN];

			strcpy(l_info->json_str, json_str);
			_wtinfo_des_encrypt(l_info->json_str, json_des_str, WTINFO_JSON_LEN, WTINFO_JSON_DES_KEY);

			memcpy(l_info->json_str, json_des_str, WTINFO_JSON_LEN);
			MD5((unsigned char*)l_info, sizeof(WTINFO), md5);

			memcpy(l_info->md5, md5, WTINFO_MD5_LEN);

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

	cJSON_Delete(json_obj);

	return ret;
}

void *wtinfo_init(void)
{
	WTINFO *l_info = NULL;
	cJSON *json_obj = NULL;
	int ret = -1;

	l_info =(WTINFO *)malloc(sizeof(WTINFO));

	if (l_info) {
		ret = _wtinfo_read(l_info);
		if (0 == ret) {
			char *des_buf;
			char md5[WTINFO_MD5_LEN];
			char orig_md5[WTINFO_MD5_LEN];
			char json_info[WTINFO_JSON_LEN];

			if (strncmp(l_info->magic, WTINFO_MAGIC, WTINFO_MAGIC_LEN)) {
				syslog(LOG_ERR, "wtinfo: magic error\n");
				goto err_out;
			}

			memcpy(orig_md5, l_info->md5, WTINFO_MD5_LEN);

			/*
			 * calc md5
			 */
			memset(l_info->md5, 0, WTINFO_MD5_LEN);
			MD5((unsigned char*)l_info, sizeof(WTINFO), md5);

			if (memcmp(md5, orig_md5, WTINFO_MD5_LEN)) {
				syslog(LOG_ERR, "wtinfo: md5 error\n");
				goto err_out;
			}

			memset(json_info, 0, WTINFO_JSON_LEN);
			_wtinfo_des_decrypt(l_info->json_str, json_info, WTINFO_JSON_LEN, WTINFO_JSON_DES_KEY);
			json_obj = cJSON_Parse(json_info);
			if (json_obj) {
				syslog(LOG_INFO, "wtinfo: init ok\n");
			} else {
				syslog(LOG_ERR, "wtinfo: init failed\n");
				goto err_out;
			}
		}

		free(l_info);		
	}

	return json_obj;	

err_out:
	if (l_info)
		free(l_info);

	return NULL;
}

void wtinfo_deinit(void *info)
{
	if (info) {
		cJSON_Delete(info);
	}
}
