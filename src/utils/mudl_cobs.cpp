/*
utils/cobs.cpp
// str8 crib'd from
// https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing
*/

#include "mudl_cobs.h"

/** COBS encode data to buffer
	@param data Pointer to input data to encode
	@param length Number of bytes to encode
	@param buffer Pointer to encoded output buffer
	@return Encoded buffer length in bytes
	@note doesn't write stop delimiter
*/

size_t mudl_cobsEncode(const void *data, size_t length, uint8_t *buffer){

	uint8_t *encode = buffer; // Encoded byte pointer
	uint8_t *codep = encode++; // Output code pointer
	uint8_t code = 1; // Code value

	for (const uint8_t *byte = (const uint8_t *)data; length--; ++byte){
		if (*byte) // Byte not zero, write it
			*encode++ = *byte, ++code;

		if (!*byte || code == 0xff){ // Input is zero or block completed, restart
			*codep = code, code = 1, codep = encode;
			if (!*byte || length)
				++encode;
		}
	}
	*codep = code;  // Write final code value
	return encode - buffer;
}

/** COBS decode data from buffer
	@param buffer Pointer to encoded input bytes
	@param length Number of bytes to decode
	@param data Pointer to decoded output data
	@return Number of bytes successfully decoded
	@note Stops decoding if delimiter byte is found
*/

size_t mudl_cobsDecode(const uint8_t *buffer, size_t length, void *data){

	const uint8_t *byte = buffer; // Encoded input byte pointer
	uint8_t *decode = (uint8_t *)data; // Decoded output byte pointer

	for (uint8_t code = 0xff, block = 0; byte < buffer + length; --block){
		if (block) // Decode block byte
			*decode++ = *byte++;
		else
		{
			if (code != 0xff) // Encoded zero, write it
				*decode++ = 0;
			block = code = *byte++; // Next block length
			if (code == 0x00) // Delimiter code found
				break;
		}
	}

	return decode - (uint8_t *)data;
}
