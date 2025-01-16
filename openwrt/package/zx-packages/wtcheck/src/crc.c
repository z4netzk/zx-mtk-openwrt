#include <stdio.h>
#include <stdlib.h>

#include "crc.h"

#define Poly 0xEDB88320L /* CRC32标准 */

static uint32_t crc_tab32[256]; /* CRC查询表 */

static void init_crc32_tab( void )
{
	int i, j;
	uint32_t crc;

	for (i=0; i<256; i++)
	{
		crc = (unsigned long)i;
		for (j=0; j<8; j++)
		{
			if ( crc & 0x00000001L )
			crc = ( crc >> 1 ) ^ Poly;
			else
			crc = crc >> 1;
		}
		crc_tab32[i] = crc;
	}
}

uint32_t get_crc32(uint32_t crcinit, uint8_t * bs, uint32_t bssize)
{
	uint32_t crc = crcinit^0xffffffff;

	init_crc32_tab();
	while(bssize--)
		crc=(crc >> 8)^crc_tab32[(crc & 0xff) ^ *bs++];

	return crc ^ 0xffffffff;
}
