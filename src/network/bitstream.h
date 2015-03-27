#ifndef M64P_NETWORK_BITSTREAM_H
#define M64P_NETWORK_BITSTREAM_H

#include <stdint.h>

#define DEFAULT_BUFSIZE (128)
#define DEFAULT_UINT32_SIZE (4)
#define DEFAULT_UINT16_SIZE (2)

typedef struct {
    int length;
    char *data;
    int datasize;
    int read_index;
} BIT_STREAM;

BIT_STREAM *BitStream_new();
void BitStream_reset(BIT_STREAM *bstream);
int BitStream_expand(BIT_STREAM *bstream);
void BitStream_free(BIT_STREAM *bstream);

int check_bitstream_size(BIT_STREAM *bstream, int size);
uint32_t BitStream_read_uint32(BIT_STREAM *bstream);
uint16_t BitStream_read_uint16(BIT_STREAM *bstream);
char* BitStream_read_char_array(BIT_STREAM *bstream, int size);
int BitStream_write_uint16(BIT_STREAM *bstream, uint16_t num);
int BitStream_write_uint32(BIT_STREAM *bstream, uint32_t num);
int BitStream_write_char_array(BIT_STREAM *bstream, char *content, int size);

#define BitStream_size(__bstream__) (__bstream__->length)
#endif /* guard */
