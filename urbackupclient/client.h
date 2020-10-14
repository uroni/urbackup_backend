#pragma once

#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Pipe.h"
#include "../Interface/Database.h"
#include "../Interface/ThreadPool.h"
#include "../urbackupcommon/os_functions.h"
#include "../urbackupcommon/filelist_utils.h"
#include "clientdao.h"
#include <map>
#include "tokens.h"
#include "ClientHash.h"

#ifdef _WIN32
#ifndef VSS_XP
#ifndef VSS_S03
#include <Vss.h>
#include <VsWriter.h>
#include <VsBackup.h>
#else
#include <win2003/vss.h>
#include <win2003/vswriter.h>
#include <win2003/vsbackup.h>
#endif //VSS_S03
#else
#include <winxp/vss.h>
#include <winxp/vswriter.h>
#include <winxp/vsbackup.h>
#endif //VSS_XP

#include "ChangeJournalWatcher.h"
#endif //_WIN32

#ifndef _WIN32
#define VSS_ID GUID
#define VSS_COMPONENT_TYPE int
#endif

#include "../fileservplugin/IFileServFactory.h"

#include <vector>
#include <string>
#include <fstream>
#include <sstream>

const int c_group_vss_components = -1;
const int c_group_default = 0;
const int c_group_continuous = 1;
const int c_group_max = 99;
const int c_group_size = 100;

const int64 async_index_timeout = 5 * 60 * 1000;
const int64 async_index_timeout_with_grace = 5 * 60 * 1000 + 30000;

const unsigned int flag_end_to_end_verification = 2;
const unsigned int flag_with_scripts = 4;
const unsigned int flag_calc_checksums = 8;
const unsigned int flag_with_orig_path = 16;
const unsigned int flag_with_sequence = 32;
const unsigned int flag_with_proper_symlinks = 64;

const uint64 change_indicator_symlink_bit = 0x4000000000000000ULL;
const uint64 change_indicator_special_bit = 0x2000000000000000ULL;
const uint64 change_indicator_all_bits = change_indicator_symlink_bit | change_indicator_special_bit;

class DirectoryWatcherThread;

class IdleCheckerThread : public IThread
{
public:
	void operator()(void);

	static bool getIdle(void);
	static bool getPause(void);
	static void setPause(bool b);

private:
	volatile static bool idle;
	volatile static bool pause;
};

enum CbtType
{
	CbtType_None,
	CbtType_Datto,
	CbtType_Era
};

struct SCRef
{
	SCRef(void): ok(false), dontincrement(false), cbt(false),
		for_imagebackup(false), with_writers(false),
		cbt_type(CbtType_None) {
#ifdef _WIN32
		backupcom = NULL;
#endif
	}

#ifdef _WIN32
	IVssBackupComponents *backupcom;
#endif
	VSS_ID ssetid;
	VSS_ID volid;
	std::string volpath;
	int64 starttime;
	std::string target;
	int save_id;
	bool ok;
	bool dontincrement;
	std::vector<std::string> starttokens;
	std::string clientsubname;
	bool cbt;
	bool for_imagebackup;
	bool with_writers;
	std::string cbt_file;
	CbtType cbt_type;
};

struct SCDirs
{
	SCDirs(void): ref(NULL) {}
	std::string dir;
	std::string target;
	std::string orig_target;
	bool running;
	SCRef *ref;
	int64 starttime;
	bool fileserv;
};

struct SCDirServerKey
{
	SCDirServerKey(std::string start_token, std::string client_subname, bool for_imagebackup)
		: start_token(start_token), client_subname(client_subname), for_imagebackup(for_imagebackup)
	{

	}

	bool operator<(const SCDirServerKey& other) const
	{
		return std::make_pair(start_token, std::make_pair(client_subname, for_imagebackup))
			< std::make_pair(other.start_token, std::make_pair(other.client_subname, other.for_imagebackup));
	}

	std::string start_token;
	std::string client_subname;
	bool for_imagebackup;
};

struct SHashedFile
{
	SHashedFile(const std::string& path,
				_i64 filesize,
				_i64 modifytime,
				const std::string& hash)
				: path(path),
				  filesize(filesize), modifytime(modifytime),
				  hash(hash)
	{
	}

	std::string path;
	_i64 filesize;
	_i64 modifytime;
	std::string hash;
};

struct SShadowCopyContext
{
#ifdef _WIN32
	SShadowCopyContext()
		:backupcom(NULL)
	{

	}

	IVssBackupComponents *backupcom;
#endif
};

struct SVssLogItem
{
	std::string msg;
	int loglevel;
	int64 times;
};

struct SBackupScript
{
	std::string scriptname;
	std::string outputname;
	int64 size;
	std::string orig_path;
	int64 lastmod;

	bool operator<(const SBackupScript &other) const
	{
		return outputname < other.outputname;
	}
};

struct SHardlinkKey
{
	bool operator==(const SHardlinkKey& other) const
	{
		return volume == other.volume
			&& frn_high == other.frn_high
			&& frn_low == other.frn_low;
	}

	std::string volume;
	int64 frn_high;
	int64 frn_low;
};

struct SHardlink
{
	SHardlinkKey key;
	int64 parent_frn_high;
	int64 parent_frn_low;
};

struct SReadError
{
	bool operator==(const SReadError& other) const
	{
		return sharename == other.sharename
			&& filepath == other.filepath;
	}

	std::string sharename;
	std::string filepath;
	int64 filepos;
	std::string msg;
};

struct SIndexInclude
{
	SIndexInclude(std::string spec, int depth, std::string prefix)
		: spec(spec), depth(depth), prefix(prefix)
	{

	}

	SIndexInclude()
		: depth(-1)
	{}

	std::string spec;
	int depth;
	std::string prefix;
};

class IVssBackupComponents;

struct SVssInstance
{
	IVssBackupComponents* backupcom;
	VSS_ID instanceId;
	VSS_ID writerId;
	VSS_COMPONENT_TYPE componentType;
	std::string componentName;
	std::string logicalPath;
	size_t refcount;
	size_t issues;
	bool set_succeeded;

	bool operator==(const SVssInstance& other) const
	{
		return instanceId == other.instanceId
			&& writerId == other.writerId
			&& componentType == other.componentType
			&& componentName == other.componentName
			&& logicalPath == other.logicalPath;
	}

	std::vector<SVssInstance*> parents;
};

class ClientDAO;
struct SVolumesCache;

class IDeregisterFileSrvScriptFn
{
public:
	virtual void deregisterFileSrvScriptFn(const std::string& fn) = 0;
};

struct SQueueRef
{
	std::auto_ptr<IMutex> mutex;
	IFile* phash_queue;
	size_t refcount;
	IDeregisterFileSrvScriptFn* dereg_fn;
	std::string fn;

	SQueueRef(IFile* phash_queue, IDeregisterFileSrvScriptFn* dereg_fn,
		std::string fn)
		: refcount(0),
		mutex(Server->createMutex()),
		phash_queue(phash_queue), dereg_fn(dereg_fn),
		fn(fn) {}

	~SQueueRef()
	{
		ScopedDeleteFile delf(phash_queue);
		if (dereg_fn != NULL)
			dereg_fn->deregisterFileSrvScriptFn(fn);
	}

	SQueueRef* ref() {
		IScopedLock lock(mutex.get());
		++refcount;
		return this;
	}

	bool deref() {
		IScopedLock lock(mutex.get());
		--refcount;
		return refcount == 0;
	}
};

#ifdef _WIN32
struct _URBCT_BITMAP_DATA;
typedef struct _URBCT_BITMAP_DATA* PURBCT_BITMAP_DATA;
#endif

class IndexThread : public IThread, public IFileServ::IReadErrorCallback, public IDeregisterFileSrvScriptFn
{
public:
	static const char IndexThreadAction_StartFullFileBackup;
	static const char IndexThreadAction_StartIncrFileBackup;
	static const char IndexThreadAction_CreateShadowcopy;
	static const char IndexThreadAction_ReleaseShadowcopy;
	static const char IndexThreadAction_GetLog;
	static const char IndexThreadAction_PingShadowCopy;
	static const char IndexThreadAction_AddWatchdir;
	static const char IndexThreadAction_RemoveWatchdir;
	static const char IndexThreadAction_RestartFilesrv;
	static const char IndexThreadAction_Stop;
	static const char IndexThreadAction_UpdateCbt;
	static const char IndexThreadAction_ReferenceShadowcopy;
	static const char IndexThreadAction_SnapshotCbt;
	static const char IndexThreadAction_WriteTokens;

	IndexThread(void);
	~IndexThread();

	void operator()(void);

	static IMutex* getFilelistMutex(void);
	static IPipe * getMsgPipe(void);
	static IFileServ *getFileSrv(void);

	static void stopIndex(void);

	static void shareDir(const std::string& token, std::string name, const std::string &path);
	static void removeDir(const std::string& token, std::string name);
	static std::string getShareDir(const std::string &name);
	static void share_dirs();
	static void unshare_dirs();
	
	static void execute_postbackup_hook(std::string scriptname, int group, const std::string& clientsubname);

	static void doStop(void);
	
	static bool backgroundBackupsEnabled(const std::string& clientsubname);

	static std::vector<std::string> buildExcludeList(const std::string& val);
	static std::vector<std::string> parseExcludePatterns(const std::string& val);
	static std::vector<SIndexInclude> parseIncludePatterns(const std::string& val);

	static bool isExcluded(const std::vector<std::string>& exclude_dirs, const std::string &path);
	static std::vector<std::string> getRmExcludedByPatterns(std::vector<std::string>& exclude_dirs, const std::string &path);
	static bool isIncluded(const std::vector<SIndexInclude>& include_dirs, const std::string &path, bool *adding_worthless);

	static std::string mapScriptOutputName(const std::string& fn);

	static std::string getSHA256(const std::string& fn);

	static int getShadowId(const std::string& volume, IFile* hdat_img);

	static bool normalizeVolume(std::string& volume);

	static void readPatterns(int index_group, std::string index_clientsubname,
		std::vector<std::string>& exclude_dirs, std::vector<SIndexInclude>& include_dirs,
		bool& backup_dirs_optional);

	void onReadError(const std::string& sharename, const std::string& filepath, int64 pos, const std::string& msg);

	void deregisterFileSrvScriptFn(const std::string& fn);

	static unsigned int getResultId();

	static bool getResult(unsigned int id, int timeoutms, std::string& result);

	static bool refResult(unsigned int id);

	static bool unrefResult(unsigned int id);

	static void removeResult(unsigned int id);

#ifndef _WIN32
	static std::string get_snapshot_script_location(const std::string& scriptname, const std::string& index_clientsubname);
#endif
	
private:
	static void addResult(unsigned int id, const std::string& res);
	static void setResultFinished(unsigned int id);

	bool readBackupDirs(void);
	bool readBackupScripts(bool full_backup);

	bool getAbsSymlinkTarget(const std::string& symlink, const std::string& orig_path, 
		std::string& target, std::string& output_target,
		const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs);
	void addSymlinkBackupDir(const std::string& target, std::string& output_target);
	bool backupNameInUse(const std::string& name);
	void removeUnconfirmedSymlinkDirs(size_t off);

	void filterEncryptedFiles(const std::string& dir, const std::string& orig_dir, std::vector<SFile>& files);
	std::vector<SFileAndHash> convertToFileAndHash(const std::string& orig_dir, const std::string& named_path, const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs, const std::vector<SFile> files, const std::string& fn_filter);
	void handleSymlinks(const std::string& orig_dir, std::string named_path, const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs, std::vector<SFileAndHash>& files);

	int64 randomChangeIndicator();

	int64 getChangeIndicator(const std::string& path);
	int64 getChangeIndicator(const SFile& file);

	enum IndexErrorInfo
	{
		IndexErrorInfo_Ok = 0,
		IndexErrorInfo_Error = 1,
		IndexErrorInfo_NoBackupPaths = 2
	};

	IndexErrorInfo indexDirs(bool full_backup, bool simultaneous_other);

	void updateDirs(void);

	void log_read_errors(const std::string& share_name, const std::string& orig_path);

	static std::string sanitizePattern(const std::string &p);	

	std::vector<SFileAndHash> getFilesProxy(const std::string &orig_path, std::string path, const std::string& named_path, bool use_db, const std::string& fn_filter, bool use_db_hashes,
		const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs, int64& target_generation);

	bool start_shadowcopy(SCDirs *dir, bool *onlyref=NULL, bool allow_restart=false, bool simultaneous_other=true, std::vector<SCRef*> no_restart_refs=std::vector<SCRef*>(),
		bool for_imagebackup=false, bool *stale_shadowcopy=NULL, bool* not_configured=NULL, bool* has_active_transaction=NULL);

	bool find_existing_shadowcopy(SCDirs *dir, bool *onlyref, bool allow_restart, bool simultaneous_other, const std::string& wpath, const std::vector<SCRef*>& no_restart_refs, bool for_imagebackup, bool *stale_shadowcopy,
		bool consider_only_own_tokens, bool share_new);
	bool release_shadowcopy(SCDirs *dir, bool for_imagebackup=false, int save_id=-1, SCDirs *dontdel=NULL);
	bool cleanup_saved_shadowcopies(bool start=false);
	std::string lookup_shadowcopy(int sid);
	void updateBackupDirsWithAll();
#ifdef _WIN32
	bool start_shadowcopy_components(VSS_ID& ssetid, bool* has_active_transaction);
	bool start_shadowcopy_win( SCDirs * dir, std::string &wpath, bool for_imagebackup, bool with_components, bool * &onlyref, bool* has_active_transaction);
	bool wait_for(IVssAsync *vsasync, const std::string& error_prefix);
	std::string GetErrorHResErrStr(HRESULT res);
	void printProviderInfo(HRESULT res);
	bool check_writer_status(IVssBackupComponents *backupcom, std::string& errmsg, 
		int loglevel, bool continue_on_failure, const std::vector<VSS_ID>& critical_writers, bool* critical_failure, bool* retryable_error);
	bool checkErrorAndLog(BSTR pbstrWriter, VSS_WRITER_STATE pState, HRESULT pHrResultFailure, std::string& errmsg, int loglevel, bool continue_on_failure, bool* retryable_error);
	bool deleteShadowcopyWin(SCDirs *dir);
	void initVss();
	bool deleteSavedShadowCopyWin(SShadowCopy& scs, SShadowCopyContext& context);
	bool getVssSettings();
	bool selectVssComponents(IVssBackupComponents *backupcom, std::vector<std::string>& selected_vols);
	bool addFilespecVol(IVssWMFiledesc* wmFile, std::vector<std::string>& selected_vols);
	bool addFiles(IVssWMFiledesc* wmFile, VSS_ID ssetid, const std::vector<SCRef*>& past_refs, std::string named_prefix,
		bool use_db, const std::vector<std::string>& exclude_files, std::fstream &outfile);
	static std::string getVolPath(const std::string& bpath);
	bool indexVssComponents(VSS_ID ssetid, bool use_db, const std::vector<SCRef*>& past_refs, std::fstream &outfile);
	bool getExcludedFiles(IVssExamineWriterMetadata* writerMetadata, UINT nExcludedFiles, std::vector<std::string>& exclude_files);
	void removeUnconfirmedVssDirs();
	std::string expandPath(BSTR pathStr);
	void removeBackupcomReferences(IVssBackupComponents *backupcom);
	bool addFileToCbt(const std::string& fpath, const DWORD& blocksize, const PURBCT_BITMAP_DATA& bitmap_data);
#else
	bool start_shadowcopy_lin( SCDirs * dir, std::string &wpath, bool for_imagebackup, bool * &onlyref, bool* not_configured);
	bool get_volumes_mounted_locally();
	bool getVssSettings() { return true; }
#endif

	bool deleteShadowcopy(SCDirs *dir);
	bool deleteSavedShadowCopy(SShadowCopy& scs, SShadowCopyContext& context);
	void clearContext( SShadowCopyContext& context);

	
	void VSSLog(const std::string& msg, int loglevel);
	void VSSLogLines(const std::string& msg, int loglevel);

	SCDirs* getSCDir(const std::string& path, const std::string& clientsubname, bool for_imagebackup);

	int execute_hook(std::string script_name, bool incr, std::string server_token, int* index_group, int* error_info=NULL);
	int execute_prebackup_hook(bool incr, std::string server_token, int index_group);
	int execute_postindex_hook(bool incr, std::string server_token, int index_group, IndexErrorInfo error_info);
	std::string execute_script(const std::string& cmd, const std::string& args);

	int execute_preimagebackup_hook(bool incr, std::string server_token);

	void start_filesrv(void);

	bool skipFile(const std::string& filepath, const std::string& namedpath,
		const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs);

	bool addMissingHashes(std::vector<SFileAndHash>* dbfiles, std::vector<SFileAndHash>* fsfiles, const std::string &orig_path,
		const std::string& filepath, const std::string& namedpath, const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs, bool calc_hashes);

	bool hasHash(const std::vector<SFileAndHash>& fsfiles);

	bool hasDirectory(const std::vector<SFileAndHash>& fsfiles);

	void modifyFilesInt(std::string path, int tgroup, const std::vector<SFileAndHash> &data, int64 target_generation);
	size_t calcBufferSize( std::string &path, const std::vector<SFileAndHash> &data );

	void commitModifyFilesBuffer();

	void addFilesInt(std::string path, int tgroup, const std::vector<SFileAndHash> &data);
	void commitAddFilesBuffer();
	std::string getShaBinary(const std::string& fn);

	std::string removeDirectorySeparatorAtEnd(const std::string& path);

	bool getShaBinary(const std::string& fn, IHashFunc& hf, bool with_cbt);

	std::string addDirectorySeparatorAtEnd(const std::string& path);

	void resetFileEntries(void);

	static void addFileExceptions(std::vector<std::string>& exclude_dirs);

	static void addHardExcludes(std::vector<std::string>& exclude_dirs);
	static void addMacosExcludes(std::vector<std::string>& exclude_dirs);


	void handleHardLinks(const std::string& bpath, const std::string& vsspath, const std::string& normalized_volume);

	void enumerateHardLinks(const std::string& volume, const std::string& vssvolume, const std::string& vsspath);

	void addResetHardlink(const std::string& volume, int64 frn_high, int64 frn_low);

	void addHardLink(const std::string& volume, int64 frn_high, int64 frn_low, int64 parent_frn_high, int64 parent_frn_low);

	void commitModifyHardLinks();

#ifdef _WIN32
	uint128 getFrn(const std::string& fn);
#endif

	std::string escapeListName(const std::string& listname);

	std::string escapeDirParam(const std::string& dir);

	void writeTokens();

	void setFlags(unsigned int flags);

	void writeDir(std::fstream& out, const std::string& name, bool with_change, uint64 change_identicator, const std::string& extra=std::string());
	bool addBackupScripts(std::fstream& outfile);

	void monitor_disk_failures();

	int get_db_tgroup();

	bool nextLastFilelistItem(SFile& data, str_map* extra, bool with_up);

	void addFromLastUpto(const std::string& fname, bool isdir, size_t depth, bool finish, std::fstream &outfile);

	void addFromLastLiftDepth(size_t depth, std::fstream &outfile);

	void addDirFromLast(std::fstream &outfile);
	void addFileFromLast(std::fstream &outfile);

	bool handleLastFilelistDepth(SFile& data);

	bool volIsEnabled(std::string settings_val, std::string volume);

	bool cbtIsEnabled(std::string clientsubname, std::string volume);

	bool crashPersistentCbtIsEnabled(std::string clientsubname, std::string volume);

	bool prepareCbt(std::string volume);

	bool finishCbt(std::string volume, int shadow_id, std::string snap_volume, bool for_image_backup, std::string cbt_file, CbtType cbt_type);

#ifndef _WIN32
	bool finishCbtDatto(IFile* volfile, IFsFile* hdat_file, IFsFile* hdat_img, std::string volume, int shadow_id, std::string snap_volume, bool for_image_backup, std::string cbt_file);
#endif

	bool finishCbtEra(IFsFile* hdat_file, IFsFile* hdat_img, std::string volume, int shadow_id, std::string snap_volume, bool for_image_backup, std::string cbt_file, int64& hdat_file_era, int64& hdat_img_era);

	bool finishCbtEra2(IFsFile* hdat, int64 hdat_era);

	bool disableCbt(std::string volume);

	void enableCbtVol(std::string volume, bool install, bool reengage);

	void updateCbt();

	void createMd5sumsFile(const std::string& path, std::string vol);

	void createMd5sumsFile(const std::string& path, const std::string& md5sums_path, IFile* output_f);

	void addScRefs(VSS_ID ssetid, std::vector<SCRef*>& out);

	void openCbtHdatFile(SCRef* ref, const std::string& sharename, const std::string& volume);

	void readSnapshotGroups();

	void readSnapshotGroup(ISettingsReader *curr_settings, const std::string& settings_name, std::vector< std::vector<std::string> >& groups);

	std::vector<std::string> getSnapshotGroup(std::string volume, bool for_image);

	std::string otherVolumeInfo(SCDirs* dir, bool onlyref);

	void postSnapshotProcessing(SCDirs* scd, bool full_backup);

	void postSnapshotProcessing(SCRef* ref, bool full_backup);

	void initParallelHashing(const std::string& async_ticket);

	bool addToPhashQueue(CWData& data);

	bool commitPhashQueue();

	bool isAllSpecialDir(const SBackupDir& bdir);

	bool punchHoleOrZero(IFile* f, int64 pos, const char* zero_buf, char* zero_read_buf, size_t zero_size);

	SVolumesCache* volumes_cache;

	std::auto_ptr<ScopedBackgroundPrio> background_prio;

	std::string starttoken;

	std::vector<SBackupDir> backup_dirs;
	std::vector< std::vector<std::string> > image_snapshot_groups;
	std::vector< std::vector<std::string> > file_snapshot_groups;

	std::vector<std::string> changed_dirs;
	std::vector<std::string> open_files;

	static IMutex *filelist_mutex;
	static IMutex *filesrv_mutex;

	static IPipe* msgpipe;

	static IMutex* cbt_shadow_id_mutex;
	static std::map<std::string, int> cbt_shadow_ids;

	unsigned int curr_result_id;

	ClientDAO *cd;

	IDatabase *db;

	static IFileServ *filesrv;

	DirectoryWatcherThread *dwt;
	THREADPOOL_TICKET dwt_ticket;

	std::map<SCDirServerKey, std::map<std::string, SCDirs*> > scdirs;
	std::vector<SCRef*> sc_refs;

	int index_c_db;
	int index_c_fs;
	int index_c_db_update;

	static volatile bool stop_index;

	std::vector<std::string> index_exclude_dirs;
	std::vector<SIndexInclude> index_include_dirs;
	bool index_backup_dirs_optional;

	int64 last_transaction_start;

	static std::map<std::string, std::string> filesrv_share_dirs;

	struct SBufferItem
	{
		SBufferItem(std::string path, int tgroup, std::vector<SFileAndHash> files, int64 target_generation)
			: path(path), tgroup(tgroup), files(files), target_generation(target_generation)
		{}

		std::string path;
		int tgroup;
		std::vector<SFileAndHash> files;
		int64 target_generation;
	};

	std::auto_ptr<ClientHash> client_hash;

	std::vector< SBufferItem > modify_file_buffer;
	size_t modify_file_buffer_size;
	std::vector< SBufferItem > add_file_buffer;
	size_t add_file_buffer_size;

	int64 last_file_buffer_commit_time;

	std::vector<SHardlinkKey> modify_hardlink_buffer_keys;
	std::vector<SHardlink> modify_hardlink_buffer;

	int index_group;
	int index_flags;
	std::string index_clientsubname;
	EBackupDirServerDefault index_server_default;
	bool index_follow_last;
	bool index_keep_files;

	SCDirs* index_scd;

	bool end_to_end_file_backup_verification;
	bool calculate_filehashes_on_client;
	bool with_scripts;
	bool with_orig_path;
	bool with_sequence;
	bool with_proper_symlinks;

	int64 last_tmp_update_time;

	std::string index_root_path;

	bool index_error;

	std::vector<SVssLogItem> vsslog;

	std::vector<SBackupScript> scripts;

	_i64 last_filebackup_filetime;

	tokens::TokenCache token_cache;
	int sha_version;

	struct SLastFileList
	{
		SLastFileList()
			: f(NULL), buf_pos(0), depth(0), depth_next(0),
			  item_pos(0), read_pos(0)
		{}

		SLastFileList(const SLastFileList& other)
		{
			*this = other;
		}

		IFile* f;
		FileListParser parser;
		std::vector<char> buf;
		size_t buf_pos;
		size_t depth;
		size_t depth_next;
		int64 item_pos;
		int64 read_pos;
		SFile item;
		str_map extra;

		SLastFileList& operator=(const SLastFileList& other)
		{
			f = other.f;
			depth = other.depth;
			depth_next = other.depth_next;
			item_pos = other.item_pos;
			read_pos = other.read_pos;
			item = other.item;
			extra = other.extra;
			return *this;
		}

		void reset_to(SLastFileList& other)
		{
			buf_pos = 0;
			parser.reset();
			depth = other.depth;
			item_pos = other.item_pos;
			read_pos = other.item_pos;
			item = other.item;
			extra = other.extra;
			depth_next = other.depth_next;
			buf.clear();

			if (f != NULL)
			{
				f->Seek(read_pos);
			}
		}
	};

	struct SRecurRet
	{
		bool has_include;
		SLastFileList backup;
		std::streampos pos;
		int64 file_id_backup;
	};

	struct SFirstInfo
	{
		std::string fn_filter;
		size_t idx;
	};

	struct SRecurParams
	{
		SRecurParams(SFileAndHash file,	SFirstInfo* first_info,
			bool curr_included,	std::string orig_dir,
			std::string dir, std::string named_path,
			size_t depth, size_t parent_idx)
			: file(file), first_info(first_info), curr_included(curr_included),
			orig_dir(orig_dir), dir(dir),
			named_path(named_path), depth(depth), state(0),
			parent_idx(parent_idx) {}

		int state;
		SFileAndHash file;
		SFirstInfo* first_info;
		bool curr_included;
		std::string orig_dir;
		std::string dir;
		std::string named_path;
		size_t depth;
		SRecurRet recur_ret;
		size_t parent_idx;
	};

	bool initialCheck(std::vector<SRecurParams>& params_stack, size_t stack_idx, const std::string& volume, const std::string& vssvolume, std::string orig_dir, std::string dir, std::string named_path,
		std::fstream &outfile, bool first, int flags, bool use_db, bool symlinked, size_t depth, bool dir_recurse, bool include_exclude_dirs,
		const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs, const std::string& orig_path);

	void initialCheckRecur1(std::vector<SRecurParams>& params_stack, SRecurParams& params, const size_t stack_idx, const std::string& volume, const std::string& vssvolume,
		std::fstream &outfile, const int flags, const bool use_db, const bool dir_recurse, const bool include_exclude_dirs,
		const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs, const std::string& orig_path);

	void initialCheckRecur2(std::vector<SRecurParams>& params_stack, SRecurParams& params, const size_t stack_idx, const std::string& volume, const std::string& vssvolume,
		std::fstream &outfile, const int flags, const bool use_db, const bool dir_recurse, const bool include_exclude_dirs,
		const std::vector<std::string>& exclude_dirs,
		const std::vector<SIndexInclude>& include_dirs, const std::string& orig_path);

	std::auto_ptr<SLastFileList> last_filelist;

	std::vector<SReadError> read_errors;
	IMutex* read_error_mutex;

	std::auto_ptr<IFsFile> index_hdat_file;
	std::map<std::string, size_t> index_hdat_sequence_ids;
	int64 index_hdat_fs_block_size;
	int64 index_chunkhash_pos;
	_u16 index_chunkhash_pos_offset;
	SQueueRef* phash_queue;
	int64 phash_queue_write_pos;
	std::vector<char> phash_queue_buffer;
	int64 file_id;

	struct SResult
	{
		ICondition* cond;
		size_t refs;
		bool finished;
		std::vector<std::string> results;
	};

	static std::map<unsigned int, SResult> index_results;
	static unsigned int next_result_id;
	static IMutex* result_mutex;

#ifdef _WIN32
	struct SComponent
	{
		std::string componentName;
		std::string logicalPath;
		VSS_ID writerId;

		bool operator==(const SComponent& other) const
		{
			return componentName == other.componentName
				&& logicalPath == other.logicalPath
				&& writerId == other.writerId;
		}
	};

	bool vss_select_all_components;
	std::vector<SComponent > vss_select_components;
	std::vector<SComponent> vss_explicitly_selected_components;
	std::vector<SComponent> vss_all_components;
	std::map<std::string, SVssInstance*> vss_name_instances;
#endif
};

std::string add_trailing_slash(const std::string &strDirName);
