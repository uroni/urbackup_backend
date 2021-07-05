#include <stdlib.h>
#include "get_chunks.h"
#include "../../external/btrfs/src/btrfs_drv.h"
#include <memory.h>

SBtrfsChunk* get_btrfs_chunks(device_extension* Vcb, size_t* n_chunks)
{
    LIST_ENTRY* le2;

    ExAcquireResourceSharedLite(&Vcb->chunk_lock, true);

    *n_chunks = 0;

    le2 = Vcb->chunks.Flink;
    while (le2 != &Vcb->chunks) {
        chunk* c = CONTAINING_RECORD(le2, chunk, list_entry);

        ++(*n_chunks);

        le2 = le2->Flink;
    }

    SBtrfsChunk* chunks_out = malloc(sizeof(SBtrfsChunk) * (*n_chunks));

    if (chunks_out == NULL)
        return chunks_out;

    size_t i = 0;
    le2 = Vcb->chunks.Flink;
    while (le2 != &Vcb->chunks) {
        chunk* c = CONTAINING_RECORD(le2, chunk, list_entry);

        SBtrfsChunk* out = chunks_out + i;
        ++i;

        out->metadata = (c->chunk_item->type & BLOCK_FLAG_METADATA) > 0 ? 1 : 0;
        CHUNK_ITEM_STRIPE* cis = (CHUNK_ITEM_STRIPE*)&c->chunk_item[1];
        out->offset = cis->offset;
        out->len = c->chunk_item->size;

        le2 = le2->Flink;
    }

    ExReleaseResourceLite(&Vcb->chunk_lock);

    return chunks_out;
}