#ifndef GOO_MIRROR_FRAME_H
#define GOO_MIRROR_FRAME_H
#include <stdint.h>
#define GOO_MIRROR_W 1024
#define GOO_MIRROR_H 768
#define GOO_MIRROR_BYTES (GOO_MIRROR_W*GOO_MIRROR_H*4)
typedef struct {
    uint32_t sequence;
    int32_t output_x, output_y;
    char output_name[96];
    unsigned char rgba[GOO_MIRROR_BYTES];
} GooMirrorFrame;
#endif
