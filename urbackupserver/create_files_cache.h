class FileCache;
struct SStartupStatus;

void create_files_cache(SStartupStatus& status);

FileCache* create_lmdb_files_cache(void);

FileCache* create_sqlite_files_cache(void);