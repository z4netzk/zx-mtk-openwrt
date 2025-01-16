#ifndef __IMAGE_H__
#define __IMAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define IH_NMLEN 32

typedef struct image_header {
	uint32_t ih_magic; /* Image Header Magic Number*/
	uint32_t ih_hcrc; /* Image Header CRC Checksum*/
	uint32_t ih_time; /* Image Creation Timestamp*/
	uint32_t ih_size; /* Image Data sizeof*/
	uint32_t ih_load; /* Data Load  Address*/
	uint32_t ih_ep; /* Entry Point Address*/
	uint32_t ih_dcrc; /* Image Data CRC Checksum*/
	uint8_t ih_os; /* Operating system.*/
	uint8_t ih_arch; /* CPU architecture*/
	uint8_t ih_type; /* Image type.*/
	uint8_t ih_comp; /* Compression type.*/
	uint8_t ih_name[IH_NMLEN]; /* Image Name*/
} image_header_t;

typedef struct md5_header {
	uint32_t magic;
	uint32_t hcrc;
	uint8_t firmware_md5[16];
	uint32_t firmware_len;
	uint8_t reserved[20];
} md5_header_t;

#define UIMAGE_HEADER_MAGIC 0x27051956

#define MD5_HEADER_MAGIC 0x19562705

#define FLASH_IMG		0

#ifdef __cplusplus
}
#endif

#endif	/* __IMAGE_H__ */
