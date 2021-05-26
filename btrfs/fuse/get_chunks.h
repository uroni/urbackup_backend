#pragma once
#include <stdint.h>
struct device_extension;

typedef struct
{
    uint64_t offset;
    uint64_t len;
    int metadata;
} SBtrfsChunk;

SBtrfsChunk* get_btrfs_chunks(device_extension* Vcb, size_t* n_chunks);