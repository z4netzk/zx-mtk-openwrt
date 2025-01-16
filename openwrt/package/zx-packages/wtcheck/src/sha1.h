/**
 * \file sha1.h
 * based from http://xyssl.org/code/source/sha1/
 *  FIPS-180-1 compliant SHA-1 implementation
 *
 *  Copyright (C) 2003-2006  Christophe Devine
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License, version 2.1 as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA	02110-1301  USA
 */
/*
 *  The SHA-1 standard was published by NIST in 1993.
 *
 *  http://www.itl.nist.gov/fipspubs/fip180-1.htm
 */
#ifndef _SHA1_H__
#define _SHA1_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SHA1_SUM_POS	-0x20
#define SHA1_SUM_LEN	20

/**
 * \brief	   SHA-1 context structure
 */
typedef struct
{
    unsigned long total[2];	/*!< number of bytes processed	*/
    unsigned long state[5];	/*!< intermediate digest state	*/
    unsigned char buffer[64];	/*!< data block being processed */
}
sha1_context;

/**
 * \brief	   SHA-1 context setup
 *
 * \param ctx	   SHA-1 context to be initialized
 */
void sha1_starts( sha1_context *ctx );

/**
 * \brief	   SHA-1 process buffer
 *
 * \param ctx	   SHA-1 context
 * \param input    buffer holding the  data
 * \param ilen	   length of the input data
 */
void sha1_update(sha1_context *ctx, const unsigned char *input,
		 unsigned int ilen);

/**
 * \brief	   SHA-1 final digest
 *
 * \param ctx	   SHA-1 context
 * \param output   SHA-1 checksum result
 */
void sha1_finish( sha1_context *ctx, unsigned char output[20] );

#ifdef __cplusplus
}
#endif

#endif /* sha1.h */
