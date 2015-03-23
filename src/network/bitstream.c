#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(WIN32)
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include "bitstream.h"

BIT_STREAM *BitStream_new(void)
{
	BIT_STREAM *bstream;

	bstream = (BIT_STREAM *)malloc(sizeof(BIT_STREAM));
	if (bstream == NULL) return NULL;

	bstream->length = 0;
	bstream->read_index = 0;
	bstream->data = (char *)malloc(DEFAULT_BUFSIZE);
	if (bstream->data == NULL) {
		free(bstream);
		return NULL;
	}
	bstream->datasize = DEFAULT_BUFSIZE;

	return bstream;
}

void BitStream_reset(BIT_STREAM *bstream)
{
	bstream->length = 0;
	bstream->read_index = 0;
}

int BitStream_expand(BIT_STREAM *bstream)
{
	char *data;

	data = (char *)realloc(bstream->data, bstream->datasize + DEFAULT_BUFSIZE);
	if (data == NULL) {
		return 0;
	}

	bstream->data = data;
	bstream->datasize += DEFAULT_BUFSIZE;

	return bstream->datasize;
}

int check_bitstream_size(BIT_STREAM *bstream, int size)
{
	if (bstream->datasize - bstream->length < size)
	{
		return BitStream_expand(bstream);
	}
	return size;
}

uint32_t BitStream_read_uint32(BIT_STREAM *bstream)
{
	char buffer[DEFAULT_UINT32_SIZE];
	memcpy(buffer, bstream->data + bstream->read_index, DEFAULT_UINT32_SIZE);

	uint32_t *p = (uint32_t*)buffer;
	uint32_t realval = ntohl(p[0]);
	bstream->read_index += DEFAULT_UINT32_SIZE;
	return realval;
}

int BitStream_write_uint32(BIT_STREAM *bstream, uint32_t num)
{
	uint32_t num_converted = htonl(num);

	int res = check_bitstream_size(bstream, DEFAULT_UINT32_SIZE);
	if (res == 0)
	{
		return res;
	}

	//copy of the value inside the stream
	memcpy(bstream->data + bstream->length, &num_converted, DEFAULT_UINT32_SIZE);

	bstream->length += DEFAULT_UINT32_SIZE;
	return bstream->length;
}

uint16_t BitStream_read_uint16(BIT_STREAM *bstream)
{
	char buffer[DEFAULT_UINT16_SIZE];
	memcpy(buffer, bstream->data + bstream->read_index, DEFAULT_UINT16_SIZE);

	uint16_t *p = (uint16_t*)buffer;
	uint16_t realval = ntohs(p[0]);
	bstream->read_index += DEFAULT_UINT16_SIZE;
	return realval;
}

int BitStream_write_uint16(BIT_STREAM *bstream, uint16_t num)
{
	uint16_t num_converted = htons(num);

	int res = check_bitstream_size(bstream, DEFAULT_UINT16_SIZE);
	if (res == 0)
	{
		return res;
	}

	//copy of the value inside the stream
	memcpy(bstream->data + bstream->length, &num_converted, DEFAULT_UINT16_SIZE);

	bstream->length += DEFAULT_UINT16_SIZE;
	return bstream->length;
}


void BitStream_free(BIT_STREAM *bstream)
{
	if (bstream != NULL) {
		free(bstream->data);
		free(bstream);
	}
}