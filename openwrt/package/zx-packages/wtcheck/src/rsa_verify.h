#ifndef __RSA_VERIFY_H__
#define __RSA_VERIFY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RSA_MAX_KEY_BITS		2048
#define RSA_MOD_LEN		(RSA_MAX_KEY_BITS/(8 * 4))

/**
 * struct rsa_public_key - holder for a public key(only 2048 bits rsa key)
 *
 * An RSA public key consists of a modulus (typically called N), the inverse
 * and R^2, where R is 2^(# key bits).
 */
struct rsa_public_key {
	uint32_t len;			/* Length of modulus[] in number of uint32_t */
	uint32_t n0inv;		/* -1 / modulus[0] mod 2^32 */
	uint32_t modulus[RSA_MOD_LEN];	/* modulus as little endian array */
	uint32_t rr[RSA_MOD_LEN];		/* R^2 as little endian array */
}__packed;

int rsa_verify(const uint8_t *b_data, const uint32_t b_len,
				const uint8_t *s_data, const uint32_t s_len,
				uint8_t *sig, uint32_t sig_len);

#ifdef __cplusplus
}
#endif

#endif	/* __RSA_VERIFY_H__ */
