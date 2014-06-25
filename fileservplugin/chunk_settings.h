#ifndef CHUNK_SETTINGS_H
#define CHUNK_SETTINGS_H

const _i64 c_checkpoint_dist=512*1024;
const _i64 c_small_hash_dist=4096;
const unsigned int c_chunk_size=4096;
const unsigned int c_chunk_padding=1+sizeof(_i64)+sizeof(_u32);

const unsigned int small_hash_size=4;
const unsigned int big_hash_size=16;

const unsigned int chunkhash_file_off=sizeof(_i64);
const unsigned int chunkhash_single_size=big_hash_size+small_hash_size*(c_checkpoint_dist/c_small_hash_dist);

const unsigned int c_reconnection_tries=30;

#endif //CHUNK_SETTINGS_H