#pragma once
#include <stdint.h>

typedef struct _device_extension device_extension;

typedef struct _BtrfsChunk
{
    uint64_t offset;
    uint64_t len;
    int metadata;
} SBtrfsChunk;

SBtrfsChunk* get_btrfs_chunks(device_extension* Vcb, size_t* n_chunks);