/*
 * Copyright (c) 2013, Google Inc.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <stdio.h>
#include <string.h>

#include <linux/types.h>

#include "sha1.h"
#include "rsa_verify.h"

#ifdef RSA_VERIFY

#define UINT64_MULT32(v, multby)  (((uint64_t)(v)) * ((uint32_t)(multby)))

#define RSA2048_BYTES	(2048 / 8)

/* This is the maximum signature length that we support, in bits */
#define RSA_MAX_SIG_BITS	2048

static const uint8_t padding_sha1_rsa2048[RSA2048_BYTES - SHA1_SUM_LEN] = {
	0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0x00, 0x30, 0x21, 0x30,
	0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a,
	0x05, 0x00, 0x04, 0x14
};

static inline uint32_t get_unaligned_be32(const uint8_t *p)
{
	return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static inline void put_unaligned_be16(uint16_t val, uint8_t *p)
{
	*p++ = val >> 8;
	*p++ = val;
}

static inline void put_unaligned_be32(uint32_t val, uint8_t *p)
{
	put_unaligned_be16(val >> 16, p);
	put_unaligned_be16(val, p + 2);
}

/**
 * subtract_modulus() - subtract modulus from the given value
 *
 * @key:	Key containing modulus to subtract
 * @num:	Number to subtract modulus from, as little endian word array
 */
static void subtract_modulus(const struct rsa_public_key *key, uint32_t num[])
{
	int64_t acc = 0;
	uint32_t i;

	for (i = 0; i < key->len; i++) {
		acc += (uint64_t)num[i] - key->modulus[i];
		num[i] = (uint32_t)acc;
		acc >>= 32;
	}
}

/**
 * greater_equal_modulus() - check if a value is >= modulus
 *
 * @key:	Key containing modulus to check
 * @num:	Number to check against modulus, as little endian word array
 * @return 0 if num < modulus, 1 if num >= modulus
 */
static int greater_equal_modulus(const struct rsa_public_key *key,
				 uint32_t num[])
{
	uint32_t i;

	for (i = key->len - 1; i >= 0; i--) {
		if (num[i] < key->modulus[i])
			return 0;
		if (num[i] > key->modulus[i])
			return 1;
	}

	return 1;  /* equal */
}

/**
 * montgomery_mul_add_step() - Perform montgomery multiply-add step
 *
 * Operation: montgomery result[] += a * b[] / n0inv % modulus
 *
 * @key:	RSA key
 * @result:	Place to put result, as little endian word array
 * @a:		Multiplier
 * @b:		Multiplicand, as little endian word array
 */
static void montgomery_mul_add_step(const struct rsa_public_key *key,
		uint32_t result[], const uint32_t a, const uint32_t b[])
{
	uint64_t acc_a, acc_b;
	uint32_t d0;
	uint32_t i;

	acc_a = (uint64_t)a * b[0] + result[0];
	d0 = (uint32_t)acc_a * key->n0inv;
	acc_b = (uint64_t)d0 * key->modulus[0] + (uint32_t)acc_a;
	for (i = 1; i < key->len; i++) {
		acc_a = (acc_a >> 32) + (uint64_t)a * b[i] + result[i];
		acc_b = (acc_b >> 32) + (uint64_t)d0 * key->modulus[i] +
				(uint32_t)acc_a;
		result[i - 1] = (uint32_t)acc_b;
	}

	acc_a = (acc_a >> 32) + (acc_b >> 32);

	result[i - 1] = (uint32_t)acc_a;

	if (acc_a >> 32)
		subtract_modulus(key, result);
}

/**
 * montgomery_mul() - Perform montgomery mutitply
 *
 * Operation: montgomery result[] = a[] * b[] / n0inv % modulus
 *
 * @key:	RSA key
 * @result:	Place to put result, as little endian word array
 * @a:		Multiplier, as little endian word array
 * @b:		Multiplicand, as little endian word array
 */
static void montgomery_mul(const struct rsa_public_key *key,
		uint32_t result[], uint32_t a[], const uint32_t b[])
{
	uint32_t i;

	for (i = 0; i < key->len; ++i)
		result[i] = 0;
	for (i = 0; i < key->len; ++i)
		montgomery_mul_add_step(key, result, a[i], b);
}

/**
 * pow_mod() - in-place public exponentiation
 *
 * @key:	RSA key
 * @inout:	Big-endian word array containing value and result
 */
static int pow_mod(const struct rsa_public_key *key, uint32_t *inout)
{
	uint32_t *result, *ptr;
	uint32_t i;

	/* Sanity check for stack size - key->len is in 32-bit words */
	if (key->len > RSA_MAX_KEY_BITS / 32) {
		return -1;
	}

	uint32_t val[key->len], acc[key->len], tmp[key->len];
	result = tmp;  /* Re-use location. */

	/* Convert from big endian byte array to little endian word array. */
	for (i = 0, ptr = inout + key->len - 1; i < key->len; i++, ptr--)
		val[i] = get_unaligned_be32((const uint8_t *)ptr);

	montgomery_mul(key, acc, val, key->rr);  /* axx = a * RR / R mod M */
	for (i = 0; i < 16; i += 2) {
		montgomery_mul(key, tmp, acc, acc); /* tmp = acc^2 / R mod M */
		montgomery_mul(key, acc, tmp, tmp); /* acc = tmp^2 / R mod M */
	}
	montgomery_mul(key, result, acc, val);  /* result = XX * a / R mod M */

	/* Make sure result < mod; result is at most 1x mod too large. */
	if (greater_equal_modulus(key, result))
		subtract_modulus(key, result);

	/* Convert to bigendian byte array */
	for (i = key->len - 1, ptr = inout; (int)i >= 0; i--, ptr++)
		put_unaligned_be32(result[i], (uint8_t *)ptr);

	return 0;
}

static int rsa_verify_key(const struct rsa_public_key *key, const uint8_t *sig,
		const uint32_t sig_len, const uint8_t *hash)
{
	const uint8_t *padding;
	int pad_len;
	int ret;

	if (!key || !sig || !hash)
		return -1;

	if (sig_len != (key->len * sizeof(uint32_t))) {
		return -1;
	}

	/* Sanity check for stack size */
	if (sig_len > RSA_MAX_SIG_BITS / 8) {
		return -1;
	}

	uint32_t buf[sig_len / sizeof(uint32_t)];

	memcpy(buf, sig, sig_len);

	ret = pow_mod(key, buf);
	if (ret)
		return ret;

	/* Determine padding to use depending on the signature type. */
	padding = padding_sha1_rsa2048;
	pad_len = RSA2048_BYTES - SHA1_SUM_LEN;

	/* Check pkcs1.5 padding bytes. */
	if (memcmp(buf, padding, pad_len)) {
		fprintf(stderr, "In RSAVerify(): Padding check failed!\n");
		return -1;
	}

	/* Check hash. */
	if (memcmp((uint8_t *)buf + pad_len, hash, sig_len - pad_len)) {
		fprintf(stderr, "In RSAVerify(): Hash check failed!\n");
		return -1;
	}

	return 0;
}

static struct rsa_public_key rsa_pub_key = {
	64,
	1582301091,
	{	
		1394594805,1495081976,1863532216,3707410435,3665992270,2202704320,
		3078387535,3486452079,4135098444,2987381899,2508428732,43513303,
		3788183974,2387265270,3476284613,2263024477,3275431167,1070550861,
		2497781018,3138578414,2703313318,1630954089,2234620577,1434739988,
		608998070,1487917579,3265592974,2904128805,2326131958,950034410,
		2651172706,768754630,3903969100,1212446250,48431338,2171582401,
		2330347046,13166528,1681377390,4005485625,1249109287,1389346530,
		1354071389,3587213186,1199299891,1708580996,3634257128,1488116685,
		3500660599,126666486,4060864217,343703753,2530363415,1661894593,3940328189,
		968739261,4063794716,1861934390,244608676,3126484363,86982830,278454267,1679279156,3459972993,
	},
	{
		4074327353,2154715878,299215851,3975539695,636866390,1347556462,3730905674,
		1467579642,4041901751,2896281055,773034651,2313324948,1470155163,3992699133,
		1556082387,2029548798,2834473610,2615843819,3465148597,2952981171,2337323573,
		609386333,592490246,2032313939,864829390,1324962347,3568799101,49936884,1457961308,
		13917750,2997774569,3371751785,4251354685,2699628990,2940466084,2359209891,3194645926,
		711913791,937286588,1967279112,2300230568,2102772374,631482649,2267093080,359859560,
		307694242,3524409102,479579197,2151828424,194767267,4043035473,886463657,2663942948,
		3064950157,513081793,1979062575,12544938,1801411660,3970278502,1370839523,70811717,1841640090,1445012790,2988186792,
	},
};

int rsa_verify(const uint8_t *b_data, const uint32_t b_len,
				const uint8_t *s_data, const uint32_t s_len,
				uint8_t *sig, uint32_t sig_len)
{
	uint8_t hash[SHA1_SUM_LEN];
	sha1_context ctx;
	int ret = 0;

	sha1_starts(&ctx);
	sha1_update(&ctx, b_data, b_len);
	sha1_update(&ctx, s_data, s_len);
	sha1_finish(&ctx, hash);

	ret = rsa_verify_key(&rsa_pub_key, sig, sig_len, hash);
	if (ret) {
		fprintf(stderr, "%s: rsa verify failed: %d\n", __func__, ret);
		return ret;
	}	

	printf("wt: firmware verify ok\n");
	return ret;
}
#else
int rsa_verify(const uint8_t *b_data, const uint32_t b_len,
				const uint8_t *s_data, const uint32_t s_len,
				uint8_t *sig, uint32_t sig_len,
				struct rsa_public_key *pub_key)
{
	return 0;
}
#endif
