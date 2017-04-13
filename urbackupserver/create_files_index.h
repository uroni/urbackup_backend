#pragma once

class FileIndex;
struct SStartupStatus;

bool create_files_index(SStartupStatus& status);

FileIndex* create_lmdb_files_index(void);

#define FILEENTRY_DEBUG(x) x
