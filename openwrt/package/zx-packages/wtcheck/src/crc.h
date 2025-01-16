#ifndef __CRC_H__
#define __CRC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

uint32_t get_crc32(uint32_t crcinit, uint8_t * bs, uint32_t bssize);

#ifdef __cplusplus
}
#endif

#endif	/* __CRC_H__ */