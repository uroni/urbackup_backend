#pragma once

#ifdef _WIN32
#include <stdint.h>
#include <stddef.h>
#include <sys/stat.h>
#endif

#include <guiddef.h>
#include <ntstatus.h>
#include <string.h>
#include <stdarg.h>
#ifndef _WIN32
#include <wchar.h>
#endif

#if !defined(_WCHAR_T_DEFINED) && defined(_WIN32)
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif

#if !defined(_WIN32) && !defined(__stdcall)
#define __stdcall
#define _stdcall
#endif

#ifndef _WIN32
#define _MSC_VER 1
#define _Requires_lock_held_(a)
#define _Requires_exclusive_lock_held_(a)
#define _Releases_lock_(a)
#define _Out_writes_bytes_opt_(a)
#define _Pre_satisfies_(a)
#define _Post_satisfies_(a)
#define _Releases_exclusive_lock_(a)
#define _Create_lock_level_(a)
#define _Lock_level_order_(a,b)
#define _Has_lock_level_(a)
#define _Requires_lock_not_held_(a)
#define _Acquires_exclusive_lock_(a)
#define _Acquires_shared_lock_(a)
#define _In_opt_
#define _In_
#define _Out_
#define _Ret_maybenull_
#define _In_reads_bytes_opt_(x)
#define _When_(x, y)
#define _Success_(x)
#define _Inout_
#define _Out_writes_bytes_(x)
#define _In_reads_bytes_(x)
#define _In_z_
#define _Out_opt_
#ifndef __cplusplus
#define __try if (1)
#define __except(x) if (0 && (x))
#define __finally if (1)
#endif
#ifndef S_IFDIR
# define S_IFDIR 0040000
#endif
#ifndef S_IFREG
# define S_IFREG 0100000
#endif
#ifndef S_IFLNK
# define S_IFLNK 0120000
#endif
#define _stricmp strcasecmp
#define MAX_PATH 255
#endif

typedef unsigned char BOOL;
typedef BOOL BOOLEAN;
typedef void* HANDLE;
typedef HANDLE* PHANDLE;
typedef wchar_t WCHAR;
typedef unsigned long ULONG;
typedef ULONG* PULONG;
typedef long LONG;
typedef unsigned short USHORT;
typedef unsigned int UINT;
typedef LONG NTSTATUS;
typedef unsigned long DWORD;
typedef unsigned short WORD;
#ifdef _WIN32
typedef unsigned __int64 ULONGLONG;
typedef __int64 LONGLONG;
#else
typedef unsigned long long int ULONGLONG;
typedef long long int LONGLONG;
#endif
typedef short CSHORT;
typedef ULONG ACCESS_MASK;
typedef unsigned char UCHAR;
typedef void* PVOID;
typedef unsigned char* PUCHAR;
typedef ULONGLONG ULONG_PTR;
typedef char KPROCESSOR_MODE;
typedef ULONGLONG ULONG64;
typedef void VOID;
typedef unsigned long ULONG32;
typedef unsigned char BYTE;
typedef unsigned int KAFFINITY;
typedef char* PCHAR;
typedef size_t SIZE_T;

#ifdef _WIN32
#define POINTER_64 __ptr64
typedef unsigned __int64 POINTER_64_INT;
#if defined(_WIN64)
#define POINTER_32 __ptr32
#else
#define POINTER_32
#endif
#else
#define POINTER_64
#define POINTER_32
typedef unsigned long long int POINTER_64_INT;
#endif

typedef union
{
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;
typedef LARGE_INTEGER* PLARGE_INTEGER;

typedef struct _FAST_MUTEX_OP FAST_MUTEX_OP;

typedef struct _FAST_MUTEX
{
    FAST_MUTEX_OP* fast_mutex;
} FAST_MUTEX;
typedef FAST_MUTEX* PFAST_MUTEX;

typedef struct _SECTION_OBJECT_POINTERS {
    void* DataSectionObject;
    void* SharedCacheMap;
    void* ImageSectionObject;
} SECTION_OBJECT_POINTERS;

typedef SECTION_OBJECT_POINTERS* PSECTION_OBJECT_POINTERS;

typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY* Flink;
    struct _LIST_ENTRY* Blink;
} LIST_ENTRY;

typedef LIST_ENTRY* PLIST_ENTRY;

typedef struct _UNICODE_STRING {
    ULONG Length;
    ULONG MaximumLength;
    WCHAR* Buffer;
} UNICODE_STRING;

typedef UNICODE_STRING* PUNICODE_STRING;
typedef PUNICODE_STRING PSTRING;

typedef struct _STRING {
    ULONG Length;
    ULONG MaximumLength;
    char* Buffer;
} STRING;

typedef struct _ANSI_STRING {
    ULONG Length;
    ULONG MaximumLength;
    char* Buffer;
} ANSI_STRING;

typedef ANSI_STRING* PANSI_STRING;

typedef void* OPLOCK;
typedef OPLOCK* POPLOCK;

BOOL FsRtlOplockIsFastIoPossible(POPLOCK lock);

typedef enum _FAST_IO_POSSIBLE {
    FastIoIsNotPossible = 0,
    FastIoIsPossible,
    FastIoIsQuestionable
} FAST_IO_POSSIBLE;

struct _ERESOURCE;
typedef struct _ERESOURCE* PERESOURCE;

typedef struct _FSRTL_ADVANCED_FCB_HEADER {
    UCHAR Version;
    OPLOCK Oplock;
    ULONG Flags;
    UCHAR Flags2;
    FAST_IO_POSSIBLE IsFastIoPossible;
    PERESOURCE PagingIoResource;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER FileSize;
    LARGE_INTEGER ValidDataLength;
    PERESOURCE Resource;
    ULONG NodeTypeCode;
    ULONG NodeByteSize;
} FSRTL_ADVANCED_FCB_HEADER;
typedef FSRTL_ADVANCED_FCB_HEADER* PFSRTL_ADVANCED_FCB_HEADER;

typedef enum _POOL_TYPE {
    NonPagedPool,
    PagedPool
} POOL_TYPE;

typedef struct _SECURITY_DESCRIPTOR {
    int dummy;
} SECURITY_DESCRIPTOR;
typedef SECURITY_DESCRIPTOR* PSECURITY_DESCRIPTOR;

typedef struct _FILE_LOCK {
    int dummy;
} FILE_LOCK;

typedef FILE_LOCK* PFILE_LOCK;

typedef struct _KTHREAD {
    void* ptr;
} KTHREAD;
typedef KTHREAD* PKTHREAD;

typedef struct _PKTHREAD* PETHREAD;

typedef struct _SHARE_ACCESS {
    int dummy;
} SHARE_ACCESS;
typedef SHARE_ACCESS* PSHARE_ACCESS;

typedef struct _KEVENT {
    LONG eword;
} KEVENT;
typedef KEVENT* PKEVENT;

typedef enum _EVENT_TYPE {
    NotificationEvent,
    SynchronizationEvent
} EVENT_TYPE;

typedef struct _KPROCESS {
    int dummy;
} _KPROCESS;
typedef _KPROCESS* PEPROCESS;

PEPROCESS PsGetCurrentProcess();

struct _VPB;
typedef struct _VPB* PVPB;
struct _DRIVER_OBJECT;
typedef struct _DRIVER_OBJECT* PDRIVER_OBJECT;
struct _DEVICE_OBJECT;
typedef struct _DEVICE_OBJECT* PDEVICE_OBJECT;
struct _DISK_DEVICE;
typedef struct _DISK_DEVICE* PDISK_DEVICE;

typedef struct _DEVICE_OBJECT {
    char StackSize;
    unsigned int Flags;
    PVOID DeviceExtension;
    PVPB Vpb;
    unsigned int Characteristics;
    PDEVICE_OBJECT AttachedDevice;
    ULONG SectorSize;
    PDRIVER_OBJECT DriverObject;
    PDISK_DEVICE DiskDevice;
    PVOID DeviceObjectExtension;
} DEVICE_OBJECT;
typedef DEVICE_OBJECT* PDEVICE_OBJECT;

typedef struct _VPB {
    short Type;
    short Size;
    unsigned short Flags;
    unsigned short VolumeLabelLength;

    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT RealDevice;

    ULONG ReferenceCount;
    WCHAR VolumeLabel[10];
} VPB;
typedef VPB* PVPB;

typedef struct _RTL_BITMAP {
    ULONG SizeOfBitMap;
    ULONG* Buffer;
} RTL_BITMAP;
typedef RTL_BITMAP* PRTL_BITMAP;

struct KSPIN_LOCK_IMPL;
typedef struct KSPIN_LOCK_IMPL* PKSPIN_LOCK_IMPL;
typedef struct _KSPIN_LOCK {
    PKSPIN_LOCK_IMPL Lock;
} KSPIN_LOCK;
typedef KSPIN_LOCK* PKSPIN_LOCK;

struct _FILE_OBJECT;
typedef struct _FILE_OBJECT* PFILE_OBJECT;

typedef struct _FILE_OBJECT {
    PVOID FsContext;
    PVOID FsContext2;
    PSECTION_OBJECT_POINTERS SectionObjectPointer;
    ULONG Flags;
    PDEVICE_OBJECT DeviceObject;
    PVPB Vpb;
    PFILE_OBJECT RelatedFileObject;
    UNICODE_STRING FileName;
    BOOLEAN DeletePending;
    LARGE_INTEGER CurrentByteOffset;
    BOOLEAN PrivateCacheMap;
} FILE_OBJECT;
typedef FILE_OBJECT* PFILE_OBJECT;

#define FO_CLEANUP_COMPLETE (1<<0)
#define FO_CACHE_SUPPORTED (1<<1)
#define FO_SYNCHRONOUS_IO (1<<2)

typedef struct _NOTIFY_SYNC {
    int dummy;
} NOTIFY_SYNC;
typedef NOTIFY_SYNC* PNOTIFY_SYNC;

typedef struct _KTIMER {
    LONG Set;
    ULONG Slot;
    LONGLONG Timer;
} KTIMER;
typedef KTIMER* PKTIMER;

typedef struct _PAGED_LOOKASIDE_LIST {
    SIZE_T entry_size;
} PAGED_LOOKASIDE_LIST;
typedef PAGED_LOOKASIDE_LIST* PPAGED_LOOKASIDE_LIST;

typedef struct _NPAGED_LOOKASIDE_LIST {
    SIZE_T entry_size;
} NPAGED_LOOKASIDE_LIST;
typedef NPAGED_LOOKASIDE_LIST* PNPAGED_LOOKASIDE_LIST;

typedef struct _SID {
    int dummy;
} SID;
typedef SID* PSID;

typedef struct _MDL {
    void* ptr;
    ULONG ByteOffset;
    ULONG MdlFlags;
    ULONG Length;
} MDL;

typedef MDL* PMDL;

typedef enum _REQMODE {
    KernelMode = 0,
    UserMode,
    MaximumMode
} REQMODE;

typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG_PTR Information;
} IO_STATUS_BLOCK;

typedef IO_STATUS_BLOCK* PIO_STATUS_BLOCK;

typedef enum _IRP_MAJ_FUNCTION {
    IRP_MJ_CREATE=0,
    IRP_MJ_CLOSE,
    IRP_MJ_WRITE,
    IRP_MJ_READ,
    IRP_MJ_QUERY_INFORMATION,
    IRP_MJ_SET_INFORMATION,
    IRP_MJ_QUERY_EA,
    IRP_MJ_SET_EA,
    IRP_MJ_FLUSH_BUFFERS,
    IRP_MJ_QUERY_VOLUME_INFORMATION,
    IRP_MJ_SET_VOLUME_INFORMATION,
    IRP_MJ_DIRECTORY_CONTROL,
    IRP_MJ_FILE_SYSTEM_CONTROL,
    IRP_MJ_DEVICE_CONTROL,
    IRP_MJ_SHUTDOWN,
    IRP_MJ_LOCK_CONTROL,
    IRP_MJ_CLEANUP,
    IRP_MJ_QUERY_SECURITY,
    IRP_MJ_SET_SECURITY,
    IRP_MJ_POWER,
    IRP_MJ_SYSTEM_CONTROL,
    IRP_MJ_PNP,
    IRP_MJ_INTERNAL_DEVICE_CONTROL
} IRP_MAJ_FUNCTION;

typedef enum _IRP_MIN_FUNCTION {
    IRP_MN_MOUNT_VOLUME,
    IRP_MN_KERNEL_CALL,
    IRP_MN_USER_FS_REQUEST,
    IRP_MN_VERIFY_VOLUME,
    IRP_MN_QUERY_POWER,
    IRP_MN_SET_POWER,
    IRP_MN_NOTIFY_CHANGE_DIRECTORY,
    IRP_MN_QUERY_DIRECTORY,
    IRP_MN_CANCEL_REMOVE_DEVICE,
    IRP_MN_QUERY_REMOVE_DEVICE,
    IRP_MN_REMOVE_DEVICE,
    IRP_MN_SURPRISE_REMOVAL,
    IRP_MN_DEVICE_USAGE_NOTIFICATION,
    IRP_MN_QUERY_CAPABILITIES,
    IRP_MN_QUERY_DEVICE_RELATIONS,
    IRP_MN_QUERY_ID,
    IRP_MN_START_DEVICE,
    IRP_MN_NORMAL,
    IRP_MN_MDL,
    IRP_MN_COMPLETE
} IRP_MIN_FUNCTION;

typedef enum _FS_INFORMATION {
    FileFsAttributeInformation,
    FileFsDeviceInformation,
    FileFsFullSizeInformation,
    FileFsObjectIdInformation,
    FileFsSizeInformation,
    FileFsVolumeInformation,
    FileFsSectorSizeInformation,
    FileFsControlInformation,
    FileFsLabelInformation
} FS_INFORMATION;

typedef enum _POWER_TYPE {
    SystemPowerState
} POWER_TYPE;

typedef enum _SYSTEM_STATE {
    PowerSystemNone,
    PowerSystemWorking
} SYSTEM_STATE;

typedef struct _ACCESS_TOKEN {
    int dummy;
} ACCESS_TOKEN;
typedef ACCESS_TOKEN* PACCESS_TOKEN;

typedef struct _SECURITY_SUBJECT_CONTEXT {
    PACCESS_TOKEN PrimaryToken;
} SECURITY_SUBJECT_CONTEXT;
typedef SECURITY_SUBJECT_CONTEXT* PSECURITY_SUBJECT_CONTEXT;

typedef struct _ACCESS_STATE {
    SECURITY_SUBJECT_CONTEXT SubjectSecurityContext;
    ULONG PreviouslyGrantedAccess;
    ULONG RemainingDesiredAccess;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
} ACCESS_STATE;
typedef ACCESS_STATE* PACCESS_STATE;

typedef struct _SECURITY_CONTEXT {
   PACCESS_STATE AccessState;
   ACCESS_MASK DesiredAccess;
} SECURITY_CONTEXT;
typedef SECURITY_CONTEXT* PSECURITY_CONTEXT;

typedef enum _FILE_INFORMATION_CLASS {
    FileNamesInformation,
    FileBothDirectoryInformation,
    FileFullDirectoryInformation,
    FileDirectoryInformation,
    FileIdFullDirectoryInformation,
    FileIdExtdDirectoryInformation,
    FileIdExtdBothDirectoryInformation,
    FileIdBothDirectoryInformation,
    FileObjectIdInformation,
    FileQuotaInformation,
    FileReparsePointInformation,
    FileAttributeTagInformation,
    FileBasicInformation,
    FileCompressionInformation,
    FileEaInformation,
    FileInternalInformation,
    FileNameInformation,
    FileNetworkOpenInformation,
    FileStandardInformation,
    FilePositionInformation,
    FileStreamInformation,
    FileHardLinkInformation,
    FileNormalizedNameInformation,
    FileStandardLinkInformation,
    FileRemoteProtocolInformation,
    FileIdInformation,
    FileStatInformation,
    FileStatLxInformation,
    FileCaseSensitiveInformation,
    FileHardLinkFullIdInformation,
    FileAllocationInformation,
    FileDispositionInformation,
    FileEndOfFileInformation,
    FileLinkInformation,
    FileRenameInformation,
    FileValidDataLengthInformation,
    FileDispositionInformationEx,
    FileRenameInformationEx,
    FileLinkInformationEx,
    FileStorageReserveIdInformation,
    FileAllInformation
} FILE_INFORMATION_CLASS;

typedef struct _FILE_GET_EA_INFORMATION {
    ULONG NextEntryOffset;
    ULONG EaNameLength;
    char EaName[1];
} FILE_GET_EA_INFORMATION;
typedef FILE_GET_EA_INFORMATION* PFILE_GET_EA_INFORMATION;

struct _FILE_ALLOCATED_RANGE_BUFFER;
typedef struct _FILE_ALLOCATED_RANGE_BUFFER* PFILE_ALLOCATED_RANGE_BUFFER;

typedef struct _PDEVICE_CAPABILITIES {
    BOOLEAN UniqueID;
    BOOLEAN SilentInstall;
} DEVICE_CAPABILITIES;
typedef DEVICE_CAPABILITIES* PDEVICE_CAPABILITIES;

typedef enum _QUERY_ID_TYPE {
    BusQueryHardwareIDs,
    BusQueryDeviceID
} QUERY_ID_TYPE;

typedef enum _DEVICE_RELATIONS_TYPE {
    BusRelations,
    TargetDeviceRelation
} DEVICE_RELATIONS_TYPE;

typedef enum _USAGE_NOTIFICATION_TYPE {
    DeviceUsageTypePaging,
    DeviceUsageTypeHibernation,
    DeviceUsageTypeDumpFile
} USAGE_NOTIFICATION_TYPE;

typedef ULONG SECURITY_INFORMATION;
typedef SECURITY_INFORMATION* PSECURITY_INFORMATION;

typedef struct _IO_STACK_LOCATION {
    IRP_MAJ_FUNCTION MajorFunction;
    IRP_MIN_FUNCTION MinorFunction;
    PFILE_OBJECT FileObject;
    ULONG Flags;

    union _Parameters
    {
        struct
        {
            ULONG Length;
            LARGE_INTEGER ByteOffset;
        } Write;
        struct
        {
            ULONG Length;
            LARGE_INTEGER ByteOffset;
        } Read;
        struct
        {
            ULONG Length;
            FS_INFORMATION FsInformationClass;            
        } QueryVolume;
        struct {
            FS_INFORMATION FsInformationClass;
        } SetVolume;
        struct {
            PDEVICE_OBJECT DeviceObject;
            PVPB Vpb;
        } MountVolume;
        struct {
            ULONG FsControlCode;
            ULONG InputBufferLength;
            ULONG OutputBufferLength;
            PFILE_ALLOCATED_RANGE_BUFFER Type3InputBuffer;
        } FileSystemControl;
        struct {
            POWER_TYPE Type;
            struct {
                SYSTEM_STATE SystemState;
            } State;
        } Power;
        struct {
            ULONG FileAttributes;
            PSECURITY_CONTEXT SecurityContext;
            ULONG ShareAccess;
            ULONG EaLength;
            ULONG Options;
        } Create;
        struct {
            FILE_INFORMATION_CLASS FileInformationClass;
            PUNICODE_STRING FileName;
            ULONG Length;
        } QueryDirectory;
        struct {
            ULONG CompletionFilter;
        } NotifyDirectory;
        struct {
            ULONG Length;
            FILE_INFORMATION_CLASS FileInformationClass;
            PFILE_OBJECT FileObject;
            BOOLEAN AdvanceOnly;
        } SetFile;
        struct {
            ULONG Length;
            FILE_INFORMATION_CLASS FileInformationClass;
        } QueryFile;
        struct {
            ULONG Length;
            ULONG EaIndex;
            PFILE_GET_EA_INFORMATION EaList;
        } QueryEa;
        struct {
            ULONG Length;
        } SetEa;
        struct {
            ULONG IoControlCode;
            ULONG InputBufferLength;
            ULONG OutputBufferLength;
            ULONG Type3InputBuffer;
        } DeviceIoControl;
        struct {
            PDEVICE_CAPABILITIES Capabilities;
        } DeviceCapabilities;
        struct {
            DEVICE_RELATIONS_TYPE Type;
        } QueryDeviceRelations;
        struct {
            QUERY_ID_TYPE IdType;
        } QueryId;
        struct {
            USAGE_NOTIFICATION_TYPE Type;
            BOOLEAN InPath;
        } UsageNotification;
        struct {
            ULONG Length;
            SECURITY_INFORMATION SecurityInformation;
        } QuerySecurity;
        struct {
            SECURITY_INFORMATION SecurityInformation;
            PSECURITY_DESCRIPTOR SecurityDescriptor;
        } SetSecurity;
    } Parameters;
} IO_STACK_LOCATION;

#define SL_OVERRIDE_VERIFY_VOLUME (1<<0)
#define SL_OPEN_PAGING_FILE (1<<1)
#define SL_FORCE_ACCESS_CHECK (1<<2)
#define SL_CASE_SENSITIVE (1<<3)
#define SL_OPEN_TARGET_DIRECTORY (1<<4)
#define SL_STOP_ON_SYMLINK (1<<5)
#define SL_INDEX_SPECIFIED (1<<6)
#define SL_RESTART_SCAN (1<<7)
#define SL_RETURN_SINGLE_ENTRY (1<<8)
#define SL_WATCH_TREE (1<<9)
#define SL_WRITE_THROUGH (1<<10)

typedef IO_STACK_LOCATION* PIO_STACK_LOCATION;

struct _IRP;
typedef struct _IRP* PIRP;
typedef NTSTATUS(__stdcall IoCompletionRoutine)(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);
typedef IoCompletionRoutine IO_COMPLETION_ROUTINE;
typedef IoCompletionRoutine* PIO_COMPLETION_ROUTINE;


typedef struct _IRP {
    PMDL MdlAddress;
    void* UserBuffer;
    void* reserved;
    char RequestorMode;
    IO_STATUS_BLOCK IoStatus;
    IO_STACK_LOCATION StackLocation;

    struct _AssociatedIrp
    {
        PVOID SystemBuffer;
    } AssociatedIrp;

    unsigned int Flags;
    PIO_STATUS_BLOCK UserIosb;
    PKEVENT UserEvent;

    struct {
        LARGE_INTEGER AllocationSize;
    } Overlay;

    struct {
        struct {
            PETHREAD Thread;
            PVOID AuxiliaryBuffer;
        } Overlay;
    } Tail;

    PIO_COMPLETION_ROUTINE CompletionRoutine;
    PVOID CompletionRoutineContext;
    ULONG CompletionRoutineFlags;

    struct _IRP* AssociatedIRP;
} IRP;
typedef IRP* PIRP;

#define COMPLETION_ROUTINE_ON_ERROR (1<<0)
#define COMPLETION_ROUTINE_ON_SUCCESS (1<<1)
#define COMPLETION_ROUTINE_ON_CANCEL (1<<2)

#define IRP_BUFFERED_IO (1<<0)
#define IRP_NOCACHE (1<<1)
#define IRP_DEALLOCATE_BUFFER (1<<2)
#define IRP_INPUT_OPERATION (1<<3)
#define IRP_PAGING_IO (1<<4)

void* MmGetSystemAddressForMdlSafe(PMDL mdl, ULONG priority);

typedef struct _CC_FILE_SIZES {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER FileSize;
    LARGE_INTEGER ValidDataLength;
} CC_FILE_SIZES;
typedef CC_FILE_SIZES* PCC_FILE_SIZES;

typedef void(_stdcall* DRIVER_UNLOAD_FUN)(PDRIVER_OBJECT);
typedef void(_stdcall* DRIVER_ADD_DEVICE_FUN)(PDRIVER_OBJECT, PDEVICE_OBJECT);
typedef NTSTATUS(_stdcall* DRIVER_MAJOR_FUN)(PDEVICE_OBJECT, PIRP);

typedef struct _DRIVER_OBJECT_EXTENSION {
    DRIVER_ADD_DEVICE_FUN AddDevice;
} DRIVER_OBJECT_EXTENSION;
typedef DRIVER_OBJECT_EXTENSION* PDRIVER_OBJECT_EXTENSION;

struct _FAST_IO_DISPATCH;
typedef struct _FAST_IO_DISPATCH* PFAST_IO_DISPATCH;

typedef struct _DRIVER_OBJECT {
    PDEVICE_OBJECT DeviceObject;

    PFAST_IO_DISPATCH FastIoDispatch;
    DRIVER_UNLOAD_FUN DriverUnload;
    PDRIVER_OBJECT_EXTENSION DriverExtension;
    DRIVER_MAJOR_FUN MajorFunction[100];
} DRIVER_OBJECT;
typedef DRIVER_OBJECT* PDRIVER_OBJECT;


typedef BOOLEAN(_stdcall *CACHE_MANAGER_ACQUIRE_FUN)(PVOID, BOOLEAN);
typedef void(_stdcall *CACHE_MANAGER_RELEASE_FUN)(PVOID);

typedef struct _CACHE_MANAGER_CALLBACKS {
    CACHE_MANAGER_ACQUIRE_FUN AcquireForLazyWrite;
    CACHE_MANAGER_RELEASE_FUN ReleaseFromLazyWrite;
    CACHE_MANAGER_ACQUIRE_FUN AcquireForReadAhead;
    CACHE_MANAGER_RELEASE_FUN ReleaseFromReadAhead;
} CACHE_MANAGER_CALLBACKS;
typedef CACHE_MANAGER_CALLBACKS* PCACHE_MANAGER_CALLBACKS;

typedef struct _REPARSE_DATA_BUFFER {
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    ULONG Reserved;
    union {
        struct {
            ULONG SubstituteNameOffset;
            ULONG SubstituteNameLength;
            ULONG PrintNameOffset;
            ULONG PrintNameLength;
            ULONG Flags;
            WCHAR PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            ULONG SubstituteNameOffset;
            ULONG PrintNameOffset;
            ULONG SubstituteNameLength;
            ULONG PrintNameLength;
            WCHAR PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
            char DataBuffer[16];
        } GenericReparseBuffer;
    };
} REPARSE_DATA_BUFFER;
typedef REPARSE_DATA_BUFFER* PREPARSE_DATA_BUFFER;

typedef struct _ECP_LIST {
    int dummy;
} ECP_LIST;
typedef ECP_LIST* PECP_LIST;

typedef enum _KWAIT_READON {
    Executive,
    UserRequest
} KWAIT_REASON;

typedef struct _IO_APC_ROUTINE {
    int dummy;
} IO_APC_ROUTINE;
typedef IO_APC_ROUTINE* PIO_APC_ROUTINE;

typedef struct _KIRQL {
    int dummy;
} KIRQL;
typedef KIRQL* PKIRQL;

struct _PEB;
typedef struct _PEB* PPEB;

typedef struct _PROCESS_BASIC_INFORMATION {
    PPEB PebBaseAddress;
} PROCESS_BASIC_INFORMATION;
typedef PROCESS_BASIC_INFORMATION* PPROCESS_BASIC_INFORMATION;

typedef enum _PROCESSINFOCLASS {
    ProcessBasicInformation
} PROCESSINFOCLASS;

typedef struct _FILE_FS_ATTRIBUTE_INFORMATION {
    unsigned int FileSystemAttributes;
    unsigned int MaximumComponentNameLength;
    unsigned int FileSystemNameLength;
    char FileSystemName[255];
} FILE_FS_ATTRIBUTE_INFORMATION;
typedef FILE_FS_ATTRIBUTE_INFORMATION* PFILE_FS_ATTRIBUTE_INFORMATION;

typedef enum _DEVICE_TYPE
{
    FILE_DEVICE_DISK,
    FILE_DEVICE_DISK_FILE_SYSTEM,
    FILE_DEVICE_UNKNOWN
} DEVICE_TYPE;

typedef struct _FILE_FS_DEVICE_INFORMATION {
    DEVICE_TYPE DeviceType;
    unsigned int Characteristics;
} FILE_FS_DEVICE_INFORMATION;
typedef FILE_FS_DEVICE_INFORMATION* PFILE_FS_DEVICE_INFORMATION;

typedef struct _FILE_FS_FULL_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER ActualAvailableAllocationUnits;
    LARGE_INTEGER CallerAvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} FILE_FS_FULL_SIZE_INFORMATION;
typedef FILE_FS_FULL_SIZE_INFORMATION* PFILE_FS_FULL_SIZE_INFORMATION;

typedef struct _FILE_FS_OBJECTID_INFORMATION_EXTENED {
    int dummy;
} FILE_FS_OBJECTID_INFORMATION_EXTENED;
typedef FILE_FS_OBJECTID_INFORMATION_EXTENED* PFILE_FS_OBJECTID_INFORMATION_EXTENED;

typedef struct _FILE_FS_OBJECTID_INFORMATION {
    UCHAR ObjectId[16];
    PFILE_FS_OBJECTID_INFORMATION_EXTENED ExtendedInfo;
} FILE_FS_OBJECTID_INFORMATION;
typedef FILE_FS_OBJECTID_INFORMATION* PFILE_FS_OBJECTID_INFORMATION;

typedef struct _FILE_FS_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER AvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} FILE_FS_SIZE_INFORMATION;

typedef struct _FILE_FS_VOLUME_INFORMATION {
    LARGE_INTEGER VolumeCreationTime;
    ULONG VolumeSerialNumber;
    ULONG VolumeLabelLength;
    BOOLEAN SupportsObjects;
    WCHAR VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION;
typedef FILE_FS_VOLUME_INFORMATION* PFILE_FS_VOLUME_INFORMATION;

typedef struct _FILE_FS_SECTOR_SIZE_INFORMATION {
    UINT LogicalBytesPerSector;
    UINT PhysicalBytesPerSectorForAtomicity;
    UINT PhysicalBytesPerSectorForPerformance;
    UINT FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
    UINT ByteOffsetForSectorAlignment;
    UINT ByteOffsetForPartitionAlignment;
    UINT Flags;
} FILE_FS_SECTOR_SIZE_INFORMATION;
typedef FILE_FS_SECTOR_SIZE_INFORMATION* PFILE_FS_SECTOR_SIZE_INFORMATION;

typedef struct _FILE_FS_LABEL_INFORMATION {
    WCHAR VolumeLabel[255];
    ULONG VolumeLabelLength;
} FILE_FS_LABEL_INFORMATION;
typedef FILE_FS_LABEL_INFORMATION* PFILE_FS_LABEL_INFORMATION;

typedef enum _ACCESS_MODE {
    IoWriteAccess,
    IoReadAccess
} ACCESS_MODE;

typedef ACCESS_MODE LOCK_OPERATION;

typedef struct _STORAGE_HOTPLUG_INFO {
    ULONG MediaRemovable;
} STORAGE_HOTPLUG_INFO;
typedef STORAGE_HOTPLUG_INFO* PSTORAGE_HOTPLUG_INFO;

typedef enum _ATA_FLAGS {
    ATA_FLAGS_DATA_IN
} ATA_FLAGS;

typedef struct _ATA_PASS_THROUGH_EX {
    ULONG Length;
    ATA_FLAGS AtaFlags;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG DataBufferOffset;
    UCHAR CurrentTaskFile[8];
} ATA_PASS_THROUGH_EX;
typedef ATA_PASS_THROUGH_EX* PATA_PASS_THROUGH_EX;

typedef enum _PROPERTY_ID {
    StorageDeviceTrimProperty
} PROPERTY_ID;

typedef enum _QUERY_TYPE {
    PropertyStandardQuery
} QUERY_TYPE;

typedef struct _STORAGE_PROPERTY_QUERY {
    PROPERTY_ID PropertyId;
    QUERY_TYPE QueryType;
    ULONG AdditionalParameters[1];
} STORAGE_PROPERTY_QUERY;
typedef STORAGE_PROPERTY_QUERY* PSTORAGE_PROPERTY_QUERY;

typedef struct _DEVICE_TRIM_DESCRIPTOR {
    BOOLEAN TrimEnabled;
} DEVICE_TRIM_DESCRIPTOR;
typedef DEVICE_TRIM_DESCRIPTOR* PDEVICE_TRIM_DESCRIPTOR;

typedef struct _STORAGE_DEVICE_NUMBER {
    ULONG DeviceNumber;
    ULONG PartitionNumber;
    ULONG DeviceType;
} STORAGE_DEVICE_NUMBER;
typedef STORAGE_DEVICE_NUMBER* PSTORAGE_DEVICE_NUMBER;

typedef struct _IDENTIFY_DEVICE_DATA {
    struct _COMMAND_SUPPORT {
        BOOLEAN FlushCache;
    } CommandSetSupport;
    ULONG NominalMediaRotationRate;
} IDENTIFY_DEVICE_DATA;
typedef IDENTIFY_DEVICE_DATA* PIDENTIFY_DEVICE_DATA;

typedef struct _OBJECT_ATTRIBUTES {
    wchar_t path[255];
} OBJECT_ATTRIBUTES;
typedef OBJECT_ATTRIBUTES* POBJECT_ATTRIBUTES;

typedef enum _OBJECT_ATTRIBUTE_TYPE {
    OBJ_KERNEL_HANDLE = 1,
    OBJ_CASE_INSENSITIVE = 2
} OBJECT_ATTRIBUTE_TYPE;

typedef void(__stdcall KSTART_ROUTINE)(PVOID Context);
typedef KSTART_ROUTINE* PKSTART_ROUTINE;

typedef struct _MOUNTDEV_NAME {
    ULONG NameLength;
    WCHAR Name[1];
} MOUNTDEV_NAME;
typedef MOUNTDEV_NAME* PMOUNTDEV_NAME;

typedef struct _GET_LENGTH_INFORMATION {
    LARGE_INTEGER Length;
} GET_LENGTH_INFORMATION;
typedef GET_LENGTH_INFORMATION* PGET_LENGTH_INFORMATION;

typedef struct _TIME_FIELDS {
    ULONG Year;
    UCHAR Month;
    UCHAR Day;
    UCHAR Hour;
    UCHAR Minute;
    UCHAR Second;
} TIME_FIELDS;
typedef TIME_FIELDS* PTIME_FIELDS;

typedef struct _FILE_STANDARD_INFORMATION {
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG NumberOfLinks;
    BOOLEAN Directory;
    BOOLEAN DeletePending;
} FILE_STANDARD_INFORMATION;
typedef FILE_STANDARD_INFORMATION* PFILE_STANDARD_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION {
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION;
typedef FILE_POSITION_INFORMATION* PFILE_POSITION_INFORMATION;

typedef enum _REPORT_INTERFACE_TYPE {
    InterfaceTypeUndefined
} REPORT_INTERFACE_TYPE;

typedef struct _DEVICE_RELATIONS {
    ULONG Count;
    PDEVICE_OBJECT Objects[1];
} DEVICE_RELATIONS;
typedef DEVICE_RELATIONS* PDEVICE_RELATIONS;

typedef enum _PLUG_N_PLAY_EVENT_CATEGORIES {
    EventCategoryDeviceInterfaceChange,
    EventCategoryTargetDeviceChange
} PLUG_N_PLAY_EVENT_CATEGORIES;

typedef NTSTATUS(_stdcall* PNPNOTIFICATION_FUN)(PVOID, PVOID);
typedef void(_stdcall* BOOTDRIVERINIT_FUN)(PDRIVER_OBJECT, PVOID, ULONG);

typedef enum _DEVICE_MANAGE_ACTION {
    DeviceDsmAction_Trim
} DEVICE_MANAGE_ACTION;

typedef struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES {
    ULONG Size;
    DEVICE_MANAGE_ACTION Action;
    ULONG Flags;
    ULONG ParameterBlockOffset;
    ULONG ParameterBlockLength;
    ULONG DataSetRangesOffset;
    ULONG DataSetRangesLength;
} DEVICE_MANAGE_DATA_SET_ATTRIBUTES;
typedef DEVICE_MANAGE_DATA_SET_ATTRIBUTES* PDEVICE_MANAGE_DATA_SET_ATTRIBUTES;

typedef struct _DEVICE_DATA_SET_RANGE {
    LONGLONG StartingOffset;
    ULONGLONG LengthInBytes;
} DEVICE_DATA_SET_RANGE;
typedef DEVICE_DATA_SET_RANGE* PDEVICE_DATA_SET_RANGE;

typedef struct _LUID {
    ULONG LowPart;
    LONG HighPart;
} LUID;
typedef LUID* PLUID;

typedef struct _MOUNTMGR_TARGET_NAME {
    ULONG DeviceNameLength;
    WCHAR DeviceName[1];
} MOUNTMGR_TARGET_NAME;
typedef MOUNTMGR_TARGET_NAME* PMOUNTMGR_TARGET_NAME;

typedef struct _KEY_VALUE_FULL_INFORMATION {
    ULONG DataOffset;
    ULONG DataLength;
    ULONG NameLength;
    ULONG Type;
    WCHAR Name[1];
} KEY_VALUE_FULL_INFORMATION;
typedef KEY_VALUE_FULL_INFORMATION* PKEY_VALUE_FULL_INFORMATION;

typedef struct _KEY_VALUE_BASIC_INFORMATION {
    ULONG NameLength;
    WCHAR Name[1];
} KEY_VALUE_BASIC_INFORMATION;
typedef KEY_VALUE_BASIC_INFORMATION* PKEY_VALUE_BASIC_INFORMATION;

typedef enum _QUERY_VALUE_TYPE {
    KeyValueFullInformation
} QUERY_VALUE_TYPE;

//static const ULONG NormalPagePriority = 1;
//static const ULONG HighPagePriority = 2;
typedef enum _PAGE_PRIORITY {
    NormalPagePriority = 1,
    HighPagePriority =2
} PAGE_PRIORITY;

typedef struct _USN {
    int dummy;
} USN;
typedef USN* PUSN;

typedef struct _FILE_FULL_EA_INFORMATION {
    ULONG EaNameLength;
    ULONG EaValueLength;
    ULONG NextEntryOffset;
    UCHAR Flags;
    char EaName[1];
} FILE_FULL_EA_INFORMATION;
typedef FILE_FULL_EA_INFORMATION* PFILE_FULL_EA_INFORMATION;

typedef struct _GENERIC_MAPPING {
    int dummy;
} GENERIC_MAPPING;
typedef GENERIC_MAPPING* PGENERIC_MAPPING;

typedef enum _FLUSH_MODE {
    MmFlushForWrite,
    MmFlushForDelete
} FLUSH_MODE;

typedef struct _FILE_ID_128 {
    ULONGLONG LowPart;
    ULONGLONG HighPart;
} FILE_ID_128;
typedef FILE_ID_128* PFILE_ID_128;

typedef struct _PRIVILEGE {
    LUID Luid;
    ULONG Attributes;
} PRIVILEGE;
typedef PRIVILEGE* PPRIVILEGE;

typedef struct _PRIVILEGE_SET {
    ULONG PrivilegeCount;
    ULONG Control;
    PRIVILEGE Privilege[5];
} PRIVILEGE_SET;
typedef PRIVILEGE_SET* PPRIVILEGE_SET;

typedef struct _FILE_BOTH_DIR_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER FileId;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    ULONG ShortNameLength;
    WCHAR FileName[1];
} FILE_BOTH_DIR_INFORMATION;
typedef FILE_BOTH_DIR_INFORMATION* PFILE_BOTH_DIR_INFORMATION;

typedef FILE_BOTH_DIR_INFORMATION FILE_DIRECTORY_INFORMATION;
typedef FILE_DIRECTORY_INFORMATION* PFILE_DIRECTORY_INFORMATION;

typedef FILE_BOTH_DIR_INFORMATION FILE_FULL_DIR_INFORMATION;
typedef FILE_FULL_DIR_INFORMATION* PFILE_FULL_DIR_INFORMATION;

typedef FILE_BOTH_DIR_INFORMATION FILE_ID_BOTH_DIR_INFORMATION;
typedef FILE_ID_BOTH_DIR_INFORMATION* PFILE_ID_BOTH_DIR_INFORMATION;

typedef FILE_BOTH_DIR_INFORMATION FILE_ID_FULL_DIR_INFORMATION;
typedef FILE_ID_FULL_DIR_INFORMATION* PFILE_ID_FULL_DIR_INFORMATION;

typedef struct _FILE_ID_EXTD_DIR_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    ULONG ShortNameLength;
    ULONG ReparsePointTag;
    struct
    {
        ULONGLONG Identifier[2];
    } FileId;
    WCHAR FileName[1];
} FILE_ID_EXTD_DIR_INFORMATION;
typedef FILE_ID_EXTD_DIR_INFORMATION* PFILE_ID_EXTD_DIR_INFORMATION;

typedef FILE_ID_EXTD_DIR_INFORMATION FILE_ID_EXTD_BOTH_DIR_INFORMATION;
typedef FILE_ID_EXTD_BOTH_DIR_INFORMATION* PFILE_ID_EXTD_BOTH_DIR_INFORMATION;

typedef FILE_BOTH_DIR_INFORMATION FILE_NAMES_INFORMATION;
typedef FILE_NAMES_INFORMATION* PFILE_NAMES_INFORMATION;

typedef struct _FILE_BASIC_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG FileAttributes;
} FILE_BASIC_INFORMATION;
typedef FILE_BASIC_INFORMATION* PFILE_BASIC_INFORMATION;

typedef struct _FILE_NETWORK_OPEN_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG FileAttributes;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
} FILE_NETWORK_OPEN_INFORMATION;
typedef FILE_NETWORK_OPEN_INFORMATION* PFILE_NETWORK_OPEN_INFORMATION;

typedef BOOLEAN(__stdcall* FAST_IO_CHECK_IF_POSSIBLE)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, BOOLEAN,
    ULONG, BOOLEAN, PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_READ)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, BOOLEAN,
    ULONG, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_WRITE)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, BOOLEAN,
    ULONG, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_QUERY_BASIC_INFO)(PFILE_OBJECT, BOOLEAN, PFILE_BASIC_INFORMATION,
    PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_QUERY_STANDARD_INFO)(PFILE_OBJECT, BOOLEAN, PFILE_STANDARD_INFORMATION,
    PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_LOCK)(PFILE_OBJECT, PLARGE_INTEGER, PLARGE_INTEGER, PEPROCESS, ULONG,
    BOOLEAN, BOOLEAN, PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_UNLOCK_SINGLE)(PFILE_OBJECT, PLARGE_INTEGER, PLARGE_INTEGER, PEPROCESS, ULONG,
    PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_UNLOCK_ALL)(PFILE_OBJECT, PEPROCESS,
    PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_UNLOCK_ALL_BY_KEY)(PFILE_OBJECT, PVOID, ULONG,
    PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_QUERY_NETWORK_OPEN_INFO)(PFILE_OBJECT, BOOLEAN, PFILE_NETWORK_OPEN_INFORMATION,
    PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef NTSTATUS(__stdcall* FAST_IO_ACQUIRE_FOR_MOD_WRITE)(PFILE_OBJECT, PLARGE_INTEGER, PERESOURCE*,
    PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_MDL_READ)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, ULONG, PMDL*,
    PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_MDL_READ_COMPLETE)(PFILE_OBJECT, PMDL, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_PREPARE_MDL_WRITE)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, ULONG, PMDL*,
    PIO_STATUS_BLOCK, PDEVICE_OBJECT);

typedef BOOLEAN(__stdcall* FAST_IO_MDL_WRITE_COMPLETE)(PFILE_OBJECT, PLARGE_INTEGER, PMDL, PDEVICE_OBJECT);

typedef NTSTATUS(__stdcall* FAST_IO_RELEASE_FOR_MOD_WRITE)(PFILE_OBJECT, PERESOURCE, PDEVICE_OBJECT);

typedef NTSTATUS(__stdcall* FAST_IO_ACQUIRE_FOR_CCFLUSH)(PFILE_OBJECT, PDEVICE_OBJECT);

typedef NTSTATUS(__stdcall* FAST_IO_RELEASE_FOR_CCFLUSH)(PFILE_OBJECT, PDEVICE_OBJECT);

typedef struct _FAST_IO_DISPATCH {
    ULONG SizeOfFastIoDispatch;
    FAST_IO_CHECK_IF_POSSIBLE FastIoCheckIfPossible;
    FAST_IO_READ FastIoRead;
    FAST_IO_WRITE FastIoWrite;
    FAST_IO_QUERY_BASIC_INFO FastIoQueryBasicInfo;
    FAST_IO_QUERY_STANDARD_INFO FastIoQueryStandardInfo;
    FAST_IO_LOCK FastIoLock;
    FAST_IO_UNLOCK_SINGLE FastIoUnlockSingle;
    FAST_IO_UNLOCK_ALL FastIoUnlockAll;
    FAST_IO_UNLOCK_ALL_BY_KEY FastIoUnlockAllByKey;
    FAST_IO_QUERY_NETWORK_OPEN_INFO FastIoQueryNetworkOpenInfo;
    FAST_IO_ACQUIRE_FOR_MOD_WRITE AcquireForModWrite;
    FAST_IO_MDL_READ MdlRead;
    FAST_IO_MDL_READ_COMPLETE MdlReadComplete;
    FAST_IO_PREPARE_MDL_WRITE PrepareMdlWrite;
    FAST_IO_MDL_WRITE_COMPLETE MdlWriteComplete;
    FAST_IO_RELEASE_FOR_MOD_WRITE ReleaseForModWrite;
    FAST_IO_ACQUIRE_FOR_CCFLUSH AcquireForCcFlush;
    FAST_IO_RELEASE_FOR_CCFLUSH ReleaseForCcFlush;
} FAST_IO_DISPATCH;
typedef FAST_IO_DISPATCH* PFAST_IO_DISPATCH;

typedef struct _FILE_DISPOSITION_INFORMATION_EX {
    ULONG Flags;
} FILE_DISPOSITION_INFORMATION_EX;
typedef FILE_DISPOSITION_INFORMATION_EX* PFILE_DISPOSITION_INFORMATION_EX;

typedef struct _FILE_DISPOSITION_INFORMATION {
    BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFORMATION;
typedef FILE_DISPOSITION_INFORMATION_EX* PFILE_DISPOSITION_INFORMATION_EX;

typedef struct _FILE_RENAME_INFORMATION {
    ULONG FileNameLength;
    ULONG Flags;
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    WCHAR FileName[1];
} FILE_RENAME_INFORMATION;
typedef FILE_RENAME_INFORMATION* PFILE_RENAME_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION {
    LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION;
typedef FILE_END_OF_FILE_INFORMATION* PFILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_LINK_INFORMATION_EX {
    int dummy;
} FILE_LINK_INFORMATION_EX;
typedef FILE_LINK_INFORMATION_EX* PFILE_LINK_INFORMATION_EX;

typedef struct _FILE_VALID_DATA_LENGTH_INFORMATION {
    LARGE_INTEGER ValidDataLength;
} FILE_VALID_DATA_LENGTH_INFORMATION;
typedef FILE_VALID_DATA_LENGTH_INFORMATION* PFILE_VALID_DATA_LENGTH_INFORMATION;

typedef struct _FILE_CASE_SENSITIVE_INFORMATION {
    ULONG Flags;
} FILE_CASE_SENSITIVE_INFORMATION;
typedef FILE_CASE_SENSITIVE_INFORMATION* PFILE_CASE_SENSITIVE_INFORMATION;

typedef struct _FILE_LINK_INFORMATION {
    ULONG Flags;
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_LINK_INFORMATION;
typedef FILE_LINK_INFORMATION* PFILE_LINK_INFORMATION;

typedef struct _FILE_INTERNAL_INFORMATION {
    LARGE_INTEGER IndexNumber;
} FILE_INTERNAL_INFORMATION;
typedef FILE_INTERNAL_INFORMATION* PFILE_INTERNAL_INFORMATION;

typedef struct _FILE_EA_INFORMATION {
    ULONG EaSize;
} FILE_EA_INFORMATION;
typedef FILE_EA_INFORMATION* PFILE_EA_INFORMATION;

typedef struct _FILE_NAME_INFORMATION {
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION;
typedef FILE_NAME_INFORMATION* PFILE_NAME_INFORMATION;

typedef struct _FILE_ATTRIBUTE_TAG_INFORMATION {
    ULONG FileAttributes;
    ULONG ReparseTag;
} FILE_ATTRIBUTE_TAG_INFORMATION;
typedef FILE_ATTRIBUTE_TAG_INFORMATION* PFILE_ATTRIBUTE_TAG_INFORMATION;

typedef struct _FILE_STREAM_INFORMATION {
    ULONG NextEntryOffset;
    ULONG StreamNameLength;
    LARGE_INTEGER StreamSize;
    LARGE_INTEGER StreamAllocationSize;
    WCHAR StreamName[1];
} FILE_STREAM_INFORMATION;
typedef FILE_STREAM_INFORMATION* PFILE_STREAM_INFORMATION;

typedef struct _FILE_STANDARD_LINK_INFORMATION {
    ULONG NumberOfAccessibleLinks;
    ULONG TotalNumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_LINK_INFORMATION;
typedef FILE_STANDARD_LINK_INFORMATION* PFILE_STANDARD_LINK_INFORMATION;

typedef struct _FILE_LINK_ENTRY_INFORMATION {
    ULONG NextEntryOffset;
    ULONGLONG ParentFileId;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_LINK_ENTRY_INFORMATION;
typedef FILE_LINK_ENTRY_INFORMATION* PFILE_LINK_ENTRY_INFORMATION;

typedef struct _FILE_LINKS_INFORMATION {
    FILE_LINK_ENTRY_INFORMATION Entry;
    ULONG EntriesReturned;
    ULONG BytesNeeded;
} FILE_LINKS_INFORMATION;
typedef FILE_LINKS_INFORMATION* PFILE_LINKS_INFORMATION;

typedef struct _FILE_LINK_ENTRY_FULL_ID_INFORMATION {
    struct {
        char Identifier[16];
    } ParentFileId;
    ULONG NextEntryOffset;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_LINK_ENTRY_FULL_ID_INFORMATION;
typedef FILE_LINK_ENTRY_FULL_ID_INFORMATION* PFILE_LINK_ENTRY_FULL_ID_INFORMATION;

typedef struct _FILE_LINKS_FULL_ID_INFORMATION {
    FILE_LINK_ENTRY_FULL_ID_INFORMATION Entry;
    ULONG EntriesReturned;
    ULONG BytesNeeded;
} FILE_LINKS_FULL_ID_INFORMATION;
typedef FILE_LINKS_FULL_ID_INFORMATION* PFILE_LINKS_FULL_ID_INFORMATION;

typedef struct _FILE_ID_INFORMATION {
    char VolumeSerialNumber[8];
    struct
    {
        char Identifier[16];
    } FileId;
} FILE_ID_INFORMATION;
typedef FILE_ID_INFORMATION* PFILE_ID_INFORMATION;

typedef struct _FILE_STAT_INFORMATION {
    FILE_ID_128 FileId;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
    ULONG ReparseTag;
    ULONG NumberOfLinks;
    ACCESS_MASK EffectiveAccess;
} FILE_STAT_INFORMATION;
typedef FILE_STAT_INFORMATION* PFILE_STAT_INFORMATION;

typedef struct _FILE_STAT_LX_INFORMATION {
    FILE_ID_128 FileId;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
    ULONG ReparseTag;
    ULONG NumberOfLinks;
    ACCESS_MASK EffectiveAccess;
    ULONG LxFlags;
    ULONG LxUid;
    ULONG LxGid;
    ULONG LxMode;
    ULONG LxDeviceIdMajor;
    ULONG LxDeviceIdMinor;
} FILE_STAT_LX_INFORMATION;
typedef FILE_STAT_LX_INFORMATION* PFILE_STAT_LX_INFORMATION;

typedef struct _FILE_COMPRESSION_INFORMATION {
    LARGE_INTEGER CompressedFileSize;
} FILE_COMPRESSION_INFORMATION;
typedef FILE_COMPRESSION_INFORMATION* PFILE_COMPRESSION_INFORMATION;

typedef struct _FILE_ACCESS_INFORMATION {
    int dummy;
} FILE_ACCESS_INFORMATION;
typedef FILE_ACCESS_INFORMATION* PFILE_ACCESS_INFORMATION;

typedef struct _FILE_MODE_INFORMATION {
    int dummy;
} FILE_MODE_INFORMATION;
typedef FILE_MODE_INFORMATION* PFILE_MODE_INFORMATION;

typedef struct _FILE_ALIGNMENT_INFORMATION {
    int dummy;
} FILE_ALIGNMENT_INFORMATION;
typedef FILE_ALIGNMENT_INFORMATION* PFILE_ALIGNMENT_INFORMATION;

typedef struct _FILE_ALL_INFORMATION {
    FILE_BASIC_INFORMATION BasicInformation;
    FILE_STANDARD_INFORMATION StandardInformation;
    FILE_INTERNAL_INFORMATION InternalInformation;
    FILE_EA_INFORMATION EaInformation;
    FILE_POSITION_INFORMATION PositionInformation;
    FILE_NAME_INFORMATION NameInformation;
} FILE_ALL_INFORMATION;
typedef FILE_ALL_INFORMATION* PFILE_ALL_INFORMATION;

typedef struct _OBJECT_TYPE {
    int dummy;
} OBJECT_TYPE;
typedef OBJECT_TYPE* POBJECT_TYPE;

extern POBJECT_TYPE* IoFileObjectType;

typedef struct _FILESYSTEM_STATISTICS {
    ULONG Version;
    ULONG FileSystemType;
    ULONG SizeOfCompleteStructure;
} FILESYSTEM_STATISTICS;
typedef FILESYSTEM_STATISTICS* PFILESYSTEM_STATISTICS;

typedef struct _FILE_SET_SPARSE_BUFFER {
    BOOLEAN SetSparse;
} FILE_SET_SPARSE_BUFFER;
typedef FILE_SET_SPARSE_BUFFER* PFILE_SET_SPARSE_BUFFER;

typedef struct _FILE_ZERO_DATA_INFORMATION {
    LARGE_INTEGER BeyondFinalZero;
    LARGE_INTEGER FileOffset;
} FILE_ZERO_DATA_INFORMATION;
typedef FILE_ZERO_DATA_INFORMATION* PFILE_ZERO_DATA_INFORMATION;

typedef struct _FILE_ALLOCATED_RANGE_BUFFER {
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER Length;
} FILE_ALLOCATED_RANGE_BUFFER;
typedef FILE_ALLOCATED_RANGE_BUFFER* PFILE_ALLOCATED_RANGE_BUFFER;

typedef struct _FILE_OBJECTID_BUFFER {
    char ObjectId[16];
    ULONG ExtendedInfo;
} FILE_OBJECTID_BUFFER;
typedef FILE_OBJECTID_BUFFER* PFILE_OBJECTID_BUFFER;

typedef struct _DRIVE_LAYOUT_INFORMATION_EX {
    ULONG PartitionCount;
} DRIVE_LAYOUT_INFORMATION_EX;
typedef DRIVE_LAYOUT_INFORMATION_EX* PDRIVE_LAYOUT_INFORMATION_EX;

typedef struct _REQUEST_OPLOCK_INPUT_BUFFER {
    ULONG Flags;
    ULONG RequestedOplockLevel;
} REQUEST_OPLOCK_INPUT_BUFFER;
typedef REQUEST_OPLOCK_INPUT_BUFFER* PREQUEST_OPLOCK_INPUT_BUFFER;

typedef struct _REQUEST_OPLOCK_OUTPUT_BUFFER {
    int dummy;
} REQUEST_OPLOCK_OUTPUT_BUFFER;
typedef REQUEST_OPLOCK_OUTPUT_BUFFER* PREQUEST_OPLOCK_OUTPUT_BUFFER;

typedef struct _REPARSE_GUID_DATA_BUFFER {
    GUID ReparseGuid;
} REPARSE_GUID_DATA_BUFFER;
typedef REPARSE_GUID_DATA_BUFFER* PREPARSE_GUID_DATA_BUFFER;

typedef struct _PFN_NUMBER {
    int dummy;
} PFN_NUMBER;

typedef struct _WORK_QUEUE_ITEM {
    int dummy;
} WORK_QUEUE_ITEM;
typedef WORK_QUEUE_ITEM* PWORK_QUEUE_ITEM;

typedef enum _REG_ENUM_TYPE {
    KeyValueBasicInformation,
    KeyBasicInformation
} REG_ENUM_TYPE;

typedef struct _KEY_BASIC_INFORMATION {
    ULONG NameLength;
    WCHAR Name[1];
} KEY_BASIC_INFORMATION;
typedef KEY_BASIC_INFORMATION* PKEY_BASIC_INFORMATION;

typedef enum _DISK_MEDIA_TYPE {
    RemovableMedia,
    FixedMedia
} DISK_MEDIA_TYPE;

typedef struct _DISK_GEOMETRY {
    ULONG BytesPerSector;
    ULONG SectorsPerTrack;
    ULONG TracksPerCylinder;
    LARGE_INTEGER Cylinders;
    DISK_MEDIA_TYPE MediaType;
} DISK_GEOMETRY;
typedef DISK_GEOMETRY* PDISK_GEOMETRY;

typedef struct _MOUNTMGR_MOUNT_POINT {
    ULONG DeviceNameOffset;
    ULONG DeviceNameLength;
    ULONG SymbolicLinkNameOffset;
    ULONG SymbolicLinkNameLength;
} MOUNTMGR_MOUNT_POINT;
typedef MOUNTMGR_MOUNT_POINT* PMOUNTMGR_MOUNT_POINT;

typedef struct _MOUNTMGR_MOUNT_POINTS {
    ULONG Size;
    ULONG NumberOfMountPoints;
    MOUNTMGR_MOUNT_POINT MountPoints[1];
} MOUNTMGR_MOUNT_POINTS;
typedef MOUNTMGR_MOUNT_POINTS* PMOUNTMGR_MOUNT_POINTS;

typedef struct _DEVICE_INTERFACE_CHANGE_NOTIFICATION {
    GUID Event;
    PUNICODE_STRING SymbolicLinkName;
} DEVICE_INTERFACE_CHANGE_NOTIFICATION;
typedef DEVICE_INTERFACE_CHANGE_NOTIFICATION* PDEVICE_INTERFACE_CHANGE_NOTIFICATION;

typedef struct _MOUNTMGR_CHANGE_NOTIFY_INFO {
    ULONG EpicNumber;
} MOUNTMGR_CHANGE_NOTIFY_INFO;
typedef MOUNTMGR_CHANGE_NOTIFY_INFO* PMOUNTMGR_CHANGE_NOTIFY_INFO;

typedef struct _ACL{
    ULONG AclRevision;
    ULONG Sbz1;
    unsigned short AclSize;
    unsigned short AceCount;
    ULONG Sbz2;
} ACL;
typedef ACL* PACL;

typedef struct _ACCESS_ALLOWED_ACE {
    struct {
        ULONG AceType;
        UCHAR AceFlags;
        ULONG AceSize;
    } Header;
    ACCESS_MASK Mask;
    char SidStart;
} ACCESS_ALLOWED_ACE;
typedef ACCESS_ALLOWED_ACE* PACCESS_ALLOWED_ACE;

typedef struct _TOKEN_OWNER{
    PSID Owner;
} TOKEN_OWNER;
typedef TOKEN_OWNER* PTOKEN_OWNER;

typedef struct _TOKEN_PRIMARY_GROUP{
    PSID PrimaryGroup;
} TOKEN_PRIMARY_GROUP;
typedef TOKEN_PRIMARY_GROUP* PTOKEN_PRIMARY_GROUP;

typedef struct __TOKEN_GROUP {
    PSID Sid;
} TOKEN_GROUP;

typedef struct _TOKEN_GROUPS {
    ULONG GroupCount;
    TOKEN_GROUP Groups[1];
} TOKEN_GROUPS;
typedef TOKEN_GROUPS* PTOKEN_GROUPS;

typedef enum _QUERY_INFORMATION_TOKEN_TYPE {
    TokenOwner,
    TokenPrimaryGroup,
    TokenGroups
} QUERY_INFORMATION_TOKEN_TYPE;

typedef struct _MOUNTDEV_UNIQUE_ID {
    ULONG UniqueIdLength;
    WCHAR UniqueId[1];
} MOUNTDEV_UNIQUE_ID;
typedef MOUNTDEV_UNIQUE_ID* PMOUNTDEV_UNIQUE_ID;

typedef struct _DISK_EXTENT {
    int dummy;
} DISK_EXTENT;
typedef DISK_EXTENT* PDISK_EXTENT;

typedef struct _VOLUME_DISK_EXTENTS {
    ULONG NumberOfDiskExtents;
    DISK_EXTENT Extents[1];
} VOLUME_DISK_EXTENTS;
typedef VOLUME_DISK_EXTENTS* PVOLUME_DISK_EXTENTS;

typedef struct _VOLUME_GET_GPT_ATTRIBUTES_INFORMATION {
    ULONG GptAttributes;
} VOLUME_GET_GPT_ATTRIBUTES_INFORMATION;
typedef VOLUME_GET_GPT_ATTRIBUTES_INFORMATION* PVOLUME_GET_GPT_ATTRIBUTES_INFORMATION;

typedef struct _MOUNTDEV_STABLE_GUID {
    GUID StableGuid;
} MOUNTDEV_STABLE_GUID;
typedef MOUNTDEV_STABLE_GUID* PMOUNTDEV_STABLE_GUID;

typedef struct _MOUNTMGR_DRIVE_LETTER_TARGET {
    ULONG DeviceNameLength;
    WCHAR DeviceName[1];
} MOUNTMGR_DRIVE_LETTER_TARGET;
typedef MOUNTMGR_DRIVE_LETTER_TARGET* PMOUNTMGR_DRIVE_LETTER_TARGET;

typedef struct _MOUNTMGR_DRIVE_LETTER_INFORMATION {
    BOOLEAN DriveLetterWasAssigned;
    WCHAR CurrentDriveLetter[1];
} MOUNTMGR_DRIVE_LETTER_INFORMATION;
typedef MOUNTMGR_DRIVE_LETTER_INFORMATION* PMOUNTMGR_DRIVE_LETTER_INFORMATION;

typedef struct _TARGET_DEVICE_REMOVAL_NOTIFICATION {
    GUID Event;
} TARGET_DEVICE_REMOVAL_NOTIFICATION;
typedef TARGET_DEVICE_REMOVAL_NOTIFICATION* PTARGET_DEVICE_REMOVAL_NOTIFICATION;

typedef struct _TOKEN_PRIVILEGE {
    LUID Luid;
    ULONG Attributes;
} TOKEN_PRIVILEGE;

typedef struct _TOKEN_PRIVILEGES {
    ULONG PrivilegeCount;
    TOKEN_PRIVILEGE Privileges[1];
} TOKEN_PRIVILEGES;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

#ifndef ANYSIZE_ARRAY
#define ANYSIZE_ARRAY 1
#endif

#define DRIVERSPECS_H
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#define __drv_aliasesMem
#define _Dispatch_type_(x)
#ifndef _Function_class_
#define _Function_class_(x)
#endif
#define NTAPI
#define __kernel_entry

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef OPTIONAL
#define OPTIONAL
#endif

#define FORCEINLINE static

#define DO_BUFFERED_IO (1<<0)
#define DO_DIRECT_IO (1<<1)
#define DO_VERIFY_VOLUME (1<<2)
#define DO_SYSTEM_BOOT_PARTITION (1<<3)
#define DO_DEVICE_INITIALIZING (1<<4)
#define DO_BUS_ENUMERATED_DEVICE (1<<5)

#define IO_DISK_INCREMENT 1
#define IO_NO_INCREMENT 0

#define IO_TYPE_VPB 1

#define VPB_DIRECT_WRITES_ALLOWED (1<<0)
#define VPB_MOUNTED (1<<1)
#define VPB_LOCKED (1<<2)
#define VPB_REMOVE_PENDING (1<<3)

#define FILE_CASE_PRESERVED_NAMES (1<<0)
#define FILE_UNICODE_ON_DISK (1<<1)
#define FILE_NAMED_STREAMS (1<<2)
#define FILE_SUPPORTS_HARD_LINKS (1<<3)
#define FILE_PERSISTENT_ACLS (1<<4)
#define FILE_SUPPORTS_REPARSE_POINTS (1<<5)
#define FILE_SUPPORTS_SPARSE_FILES (1<<6)
#define FILE_SUPPORTS_OBJECT_IDS (1<<7)
#define FILE_SUPPORTS_OPEN_BY_FILE_ID (1<<8)
#define FILE_SUPPORTS_EXTENDED_ATTRIBUTES (1<<9)
#define FILE_SUPPORTS_BLOCK_REFCOUNTING (1<<10)
#define FILE_SUPPORTS_POSIX_UNLINK_RENAME (1<<11)
#define FILE_READ_ONLY_VOLUME (1<<12)
#define FILE_CASE_SENSITIVE_SEARCH (1<<13)

#define FILE_READ_ONLY_DEVICE 1

#define SSINFO_FLAGS_ALIGNED_DEVICE 1
#define SSINFO_FLAGS_PARTITION_ALIGNED_ON_DEVICE 2
#define SSINFO_FLAGS_TRIM_ENABLED 4

#define EXCEPTION_EXECUTE_HANDLER 0

#define FILE_NOTIFY_CHANGE_LAST_WRITE (1<<0)
#define FILE_NOTIFY_CHANGE_DIR_NAME (1<<1)
#define FILE_NOTIFY_CHANGE_FILE_NAME (1<<2)
#define FILE_NOTIFY_CHANGE_STREAM_NAME (1<<3)
#define FILE_NOTIFY_CHANGE_SIZE (1<<4)
#define FILE_NOTIFY_CHANGE_ATTRIBUTES (1<<5)
#define FILE_NOTIFY_CHANGE_CREATION (1<<6)
#define FILE_NOTIFY_CHANGE_LAST_ACCESS (1<<7)
#define FILE_NOTIFY_CHANGE_STREAM_SIZE (1<<8)
#define FILE_NOTIFY_CHANGE_EA (1<<9)
#define FILE_NOTIFY_CHANGE_SECURITY (1<<10)
#define FILE_NOTIFY_CHANGE_STREAM_WRITE (1<<11)

#define REG_NOTIFY_CHANGE_LAST_SET (1<<0)

#define FILE_ACTION_MODIFIED 1
#define FILE_ACTION_REMOVED 2
#define FILE_ACTION_ADDED 3
#define FILE_ACTION_ADDED_STREAM 4
#define FILE_ACTION_REMOVED_STREAM 5
#define FILE_ACTION_RENAMED_OLD_NAME 6
#define FILE_ACTION_RENAMED_NEW_NAME 7
#define FILE_ACTION_MODIFIED_STREAM 8

#define FSRTL_FCB_HEADER_V2 (2)
#define FSRTL_FLAG2_IS_PAGING_FILE (1)
#define FSRTL_VOLUME_UNLOCK 3
#define FSRTL_VOLUME_MOUNT 4
#define FSRTL_VOLUME_CHANGE_SIZE 5
#define FSRTL_VOLUME_FORCED_CLOSED 6
#define FSRTL_VOLUME_LOCK 7
#define FSRTL_VOLUME_LOCK_FAILED 8
#define FSRTL_VOLUME_DISMOUNT 9

#define FILE_ATTRIBUTE_DIRECTORY (1<<0)
#define FILE_ATTRIBUTE_REPARSE_POINT (1<<1)
#define FILE_ATTRIBUTE_READONLY (1<<2)
#define FILE_ATTRIBUTE_HIDDEN (1<<3)
#define FILE_ATTRIBUTE_ARCHIVE (1<<4)
#define FILE_ATTRIBUTE_NORMAL (1<<5)
#define FILE_ATTRIBUTE_TEMPORARY (1<<6)
#define FILE_ATTRIBUTE_SYSTEM (1<<7)

#define FILE_CS_FLAG_CASE_SENSITIVE_DIR (1<<0)

//Characteristics
#define FILE_REMOVABLE_MEDIA (1<<6)

#define IOCTL_STORAGE_GET_HOTPLUG_INFO 1
#define IOCTL_STORAGE_CHECK_VERIFY 2
#define IOCTL_STORAGE_GET_DEVICE_NUMBER 3
#define IOCTL_DISK_IS_WRITABLE 4
#define IOCTL_ATA_PASS_THROUGH 5
#define IOCTL_STORAGE_QUERY_PROPERTY 6
#define IOCTL_MOUNTDEV_QUERY_DEVICE_NAME 7
#define IOCTL_DISK_GET_LENGTH_INFO 8
#define IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES 9
#define IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION 10
#define IOCTL_DISK_GET_DRIVE_LAYOUT_EX 11
#define IOCTL_DISK_GET_DRIVE_GEOMETRY 12
#define IOCTL_MOUNTMGR_DELETE_POINTS 13
#define IOCTL_VOLUME_ONLINE 14
#define IOCTL_MOUNTMGR_CHANGE_NOTIFY 15
#define IOCTL_MOUNTMGR_QUERY_POINTS 16
#define IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS 17
#define IOCTL_MOUNTDEV_QUERY_UNIQUE_ID 18
#define IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME 19
#define IOCTL_MOUNTDEV_QUERY_STABLE_GUID 20
#define IOCTL_MOUNTDEV_LINK_CREATED 21
#define IOCTL_VOLUME_GET_GPT_ATTRIBUTES 22
#define IOCTL_DISK_CHECK_VERIFY 23
#define IOCTL_VOLUME_BASE 24
#define IOCTL_MOUNTMGR_NEXT_DRIVE_LETTER 25
#define IOCTL_DISK_UPDATE_PROPERTIES 26


#define IDE_COMMAND_IDENTIFY 1

#define FILE_DEVICE_SECURE_OPEN (1<<0)
#define FILE_AUTOGENERATED_DEVICE_NAME (1<<1)

#define MOUNTMGR_DEVICE_NAME L"MountMgr"

#define FILE_SHARE_READ (1<<0)
#define FILE_SHARE_WRITE (1<<1)

#define FILE_NON_DIRECTORY_FILE (1<<0)
#define FILE_WRITE_THROUGH (1<<1)
#define FILE_SYNCHRONOUS_IO_ALERT (1<<2)
#define FILE_SEQUENTIAL_ONLY (1<<3)
#define FILE_NO_INTERMEDIATE_BUFFERING (1<<4)
#define FILE_SYNCHRONOUS_IO_NONALERT (1<<5)
#define FILE_CREATE_TREE_CONNECTION (1<<6)
#define FILE_COMPLETE_IF_OPLOCKED (1<<7)
#define FILE_NO_EA_KNOWLEDGE (1<<8)
#define FILE_DIRECTORY_FILE (1<<9)
#define FILE_OPEN_REMOTE_INSTANCE (1<<10)
#define FILE_RANDOM_ACCESS (1<<11)
#define FILE_OPEN_BY_FILE_ID (1<<12)
#define FILE_DELETE_ON_CLOSE (1<<13)
#define FILE_OPEN_FOR_BACKUP_INTENT (1<<14)
#define FILE_NO_COMPRESSION (1<<15)
#define FILE_OPEN_REQUIRING_OPLOCK (1<<16)
#define FILE_DISALLOW_EXCLUSIVE (1<<17)
#define FILE_RESERVE_OPFILTER (1<<18)
#define FILE_OPEN_REPARSE_POINT (1<<19)
#define FILE_OPEN_NO_RECALL (1<<20)
#define FILE_OPEN_FOR_FREE_SPACE_QUERY (1<<21)

#define FILE_VALID_OPTION_FLAGS (0xffffffff)

#define FILE_DISPOSITION_DELETE (1<<0)
#define FILE_DISPOSITION_FORCE_IMAGE_SECTION_CHECK (1<<1)
#define FILE_DISPOSITION_POSIX_SEMANTICS (1<<2)

//Rename
#define FILE_RENAME_IGNORE_READONLY_ATTRIBUTE (1<<0)
#define FILE_RENAME_REPLACE_IF_EXISTS (1<<1)
#define FILE_RENAME_POSIX_SEMANTICS (1<<2)

#define FILE_LINK_REPLACE_IF_EXISTS (1<<0)
#define FILE_LINK_POSIX_SEMANTICS (1<<1)
#define FILE_LINK_IGNORE_READONLY_ATTRIBUTE (1<<2)

//Open Disposition
#define FILE_SUPERSEDE 1
#define FILE_OVERWRITE 2
#define FILE_OVERWRITE_IF 3
#define FILE_OPEN 4
#define FILE_OPEN_IF 5
#define FILE_CREATE 6

#define FILE_OPENED (1<<0)
#define FILE_OVERWRITTEN (1<<1)
#define FILE_SUPERSEDED (1<<2)
#define FILE_CREATED (1<<3)

#define NTDDI_VISTA 6
#define NTDDI_WIN7 7
#define NTDDI_WIN8 8

#define KEY_QUERY_VALUE (1<<0)
#define KEY_ENUMERATE_SUB_KEYS (1<<1)
#define KEY_NOTIFY (1<<2)
#define KEY_SET_VALUE (1<<3)

#define REG_OPTION_NON_VOLATILE (1<<0)
#define REG_OPENED_EXISTING_KEY (1<<1)
#define REG_CREATED_NEW_KEY (1<<2)
#define REG_SZ (1<<3)
#define REG_EXPAND_SZ (1<<4)

#define PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES (1<<0)

#define DEVICE_DSM_FLAG_TRIM_NOT_FS_ALLOCATED (1<<0)
#define DEVICE_DSM_FLAG_ENTIRE_DATA_SET_RANGE (1<<1)

#define SE_MANAGE_VOLUME_PRIVILEGE 1
#define SE_TCB_PRIVILEGE 2

#define GENERIC_READ 1

#define FSRTL_CACHE_TOP_LEVEL_IRP ((PVOID)0x01)

#define FILE_ADD_SUBDIRECTORY 1
#define FILE_ADD_FILE 2

#define IO_REPARSE_TAG_SYMLINK 2
#define IO_REPARSE_TAG_MOUNT_POINT 3
#define IO_REPARSE_TAG_RESERVED_ZERO 0
#define IO_REPARSE_TAG_RESERVED_ONE 1

#define SYMLINK_FLAG_RELATIVE (1<<0)

#define FILE_READ_ATTRIBUTES (1<<0)
#define FILE_WRITE_DATA (1<<1)
#define READ_CONTROL (1<<2)
#define SYNCHRONIZE (1<<3)
#define ACCESS_SYSTEM_SECURITY (1<<4)
#define FILE_READ_DATA (1<<5)
#define FILE_READ_EA (1<<6)
#define FILE_EXECUTE (1<<7)
#define FILE_LIST_DIRECTORY (1<<8)
#define FILE_TRAVERSE (1<<9)
#define DELETE (1<<10)
#define WRITE_OWNER (1<<11)
#define WRITE_DAC (1<<12)
#define FILE_WRITE_EA (1<<13)
#define FILE_WRITE_ATTRIBUTES (1<<14)
#define FILE_DELETE_CHILD (1<<15)
#define FILE_APPEND_DATA (1<<16)
#define FILE_ANY_ACCESS (1<<17)
#define FILE_WRITE_ACCESS (1<<18)
#define FILE_ALL_ACCESS 0xFFF
#define OBJECT_INHERIT_ACE (1<<19)
#define CONTAINER_INHERIT_ACE (1<<20)
#define INHERIT_ONLY_ACE (1<<21)
#define FILE_GENERIC_READ (1<<22)
#define FILE_GENERIC_EXECUTE (1<<23)
#define FILE_GENERIC_WRITE (1<<24)
#define FILE_READ_ACCESS (1<<25)

#define METHOD_BUFFERED 1
#define METHOD_OUT_DIRECT 2
#define METHOD_IN_DIRECT 3
#define METHOD_NEITHER 4

#define FILE_DEVICE_FILE_SYSTEM 1
#define FILE_DEVICE_UNKNOWN 2

//DesiredAccess
#define MAXIMUM_ALLOWED (1<<1)

//EA flags
#define FILE_NEED_EA (1<<1)

//Privilege set control
#define PRIVILEGE_SET_ALL_NECESSARY 1

#define LX_FILE_METADATA_HAS_UID (1<<0)
#define LX_FILE_METADATA_HAS_GID (1<<1)
#define LX_FILE_METADATA_HAS_MODE (1<<2)
#define LX_FILE_METADATA_HAS_DEVICE_ID (1<<3)
#define LX_FILE_CASE_SENSITIVE_DIR (1<<4)

#define MDL_PAGES_LOCKED (1<<0)
#define MDL_PARTIAL (1<<1)

#define IDE_COMMAND_FLUSH_CACHE 1

#define UInt32x32To64(a, b) (((ULONGLONG)a)*((ULONGLONG)b))

#define FILESYSTEM_STATISTICS_TYPE_NTFS 1

#define COMPRESSION_FORMAT_NONE 0

#define FSCTL_REQUEST_OPLOCK 1
#define FSCTL_REQUEST_OPLOCK_LEVEL_1 2
#define FSCTL_REQUEST_OPLOCK_LEVEL_2 3
#define FSCTL_REQUEST_BATCH_OPLOCK 4
#define FSCTL_REQUEST_FILTER_OPLOCK 5
#define FSCTL_OPLOCK_BREAK_ACKNOWLEDGE 6
#define FSCTL_OPBATCH_ACK_CLOSE_PENDING 7
#define FSCTL_OPLOCK_BREAK_NOTIFY 8
#define FSCTL_OPLOCK_BREAK_ACK_NO_2 9
#define FSCTL_LOCK_VOLUME 10
#define FSCTL_UNLOCK_VOLUME 11
#define FSCTL_DISMOUNT_VOLUME 12
#define FSCTL_IS_VOLUME_MOUNTED 13
#define FSCTL_IS_PATHNAME_VALID 14
#define FSCTL_MARK_VOLUME_DIRTY 15
#define FSCTL_QUERY_RETRIEVAL_POINTERS 16
#define FSCTL_GET_COMPRESSION 17
#define FSCTL_SET_COMPRESSION 18
#define FSCTL_SET_BOOTLOADER_ACCESSED 19
#define FSCTL_INVALIDATE_VOLUMES 20
#define FSCTL_QUERY_FAT_BPB 21
#define FSCTL_FILESYSTEM_GET_STATISTICS 22
#define FSCTL_GET_NTFS_VOLUME_DATA 23
#define FSCTL_GET_NTFS_FILE_RECORD 24
#define FSCTL_GET_VOLUME_BITMAP 25
#define FSCTL_GET_RETRIEVAL_POINTERS 26
#define FSCTL_MOVE_FILE 27
#define FSCTL_IS_VOLUME_DIRTY 28
#define FSCTL_ALLOW_EXTENDED_DASD_IO 29
#define FSCTL_FIND_FILES_BY_SID 30
#define FSCTL_SET_OBJECT_ID 31
#define FSCTL_GET_OBJECT_ID 32
#define FSCTL_DELETE_OBJECT_ID 33
#define FSCTL_SET_REPARSE_POINT 34
#define FSCTL_GET_REPARSE_POINT 35
#define FSCTL_DELETE_REPARSE_POINT 36
#define FSCTL_ENUM_USN_DATA 37
#define FSCTL_SECURITY_ID_CHECK 38
#define FSCTL_READ_USN_JOURNAL 39
#define FSCTL_SET_OBJECT_ID_EXTENDED 40
#define FSCTL_CREATE_OR_GET_OBJECT_ID 41
#define FSCTL_SET_SPARSE 42
#define FSCTL_SET_ZERO_DATA 43
#define FSCTL_QUERY_ALLOCATED_RANGES 44
#define FSCTL_ENABLE_UPGRADE 45
#define FSCTL_SET_ENCRYPTION 46
#define FSCTL_ENCRYPTION_FSCTL_IO 47
#define FSCTL_WRITE_RAW_ENCRYPTED 48
#define FSCTL_READ_RAW_ENCRYPTED 49
#define FSCTL_CREATE_USN_JOURNAL 50
#define FSCTL_READ_FILE_USN_DATA 51
#define FSCTL_WRITE_USN_CLOSE_RECORD 52
#define FSCTL_EXTEND_VOLUME 53
#define FSCTL_QUERY_USN_JOURNAL 54
#define FSCTL_DELETE_USN_JOURNAL 55
#define FSCTL_MARK_HANDLE 56
#define FSCTL_SIS_COPYFILE 57
#define FSCTL_SIS_LINK_FILES 58
#define FSCTL_RECALL_FILE 59
#define FSCTL_READ_FROM_PLEX 60
#define FSCTL_FILE_PREFETCH 61
#define FSCTL_MAKE_MEDIA_COMPATIBLE 62
#define FSCTL_SET_DEFECT_MANAGEMENT 63
#define FSCTL_QUERY_SPARING_INFO 64
#define FSCTL_QUERY_ON_DISK_VOLUME_INFO 65
#define FSCTL_SET_VOLUME_COMPRESSION_STATE 66
#define FSCTL_TXFS_MODIFY_RM 67
#define FSCTL_TXFS_QUERY_RM_INFORMATION 68
#define FSCTL_TXFS_ROLLFORWARD_REDO 69
#define FSCTL_TXFS_ROLLFORWARD_UNDO 70
#define FSCTL_TXFS_START_RM 71
#define FSCTL_TXFS_SHUTDOWN_RM 72
#define FSCTL_TXFS_READ_BACKUP_INFORMATION 73
#define FSCTL_TXFS_WRITE_BACKUP_INFORMATION 74
#define FSCTL_TXFS_CREATE_SECONDARY_RM 75
#define FSCTL_TXFS_GET_METADATA_INFO 76
#define FSCTL_TXFS_GET_TRANSACTED_VERSION 77
#define FSCTL_TXFS_SAVEPOINT_INFORMATION 78
#define FSCTL_TXFS_CREATE_MINIVERSION 79
#define FSCTL_TXFS_TRANSACTION_ACTIVE 80
#define FSCTL_SET_ZERO_ON_DEALLOCATION 81
#define FSCTL_SET_REPAIR 82
#define FSCTL_GET_REPAIR 83
#define FSCTL_WAIT_FOR_REPAIR 84
#define FSCTL_INITIATE_REPAIR 85
#define FSCTL_CSC_INTERNAL 86
#define FSCTL_SHRINK_VOLUME 87
#define FSCTL_SET_SHORT_NAME_BEHAVIOR 88
#define FSCTL_DFSR_SET_GHOST_HANDLE_STATE 89
#define FSCTL_TXFS_LIST_TRANSACTION_LOCKED_FILES 90
#define FSCTL_TXFS_LIST_TRANSACTIONS 91
#define FSCTL_QUERY_PAGEFILE_ENCRYPTION 92
#define FSCTL_RESET_VOLUME_ALLOCATION_HINTS 93
#define FSCTL_TXFS_READ_BACKUP_INFORMATION2 94
#define FSCTL_CSV_CONTROL 95
#define FSCTL_QUERY_VOLUME_CONTAINER_STATE 96

#define CTL_CODE(a, b, c, d) ((a)*1000000+(b)*1000000+(c)*1000+(d))


#define REQUEST_OPLOCK_INPUT_FLAG_REQUEST (1<<0)
#define REQUEST_OPLOCK_INPUT_FLAG_ACK (1<<1)

#define OPLOCK_LEVEL_CACHE_WRITE (1<<0)
#define OPLOCK_LEVEL_CACHE_HANDLE (1<<1)

#define REPARSE_DATA_BUFFER_HEADER_SIZE 16
#define REPARSE_GUID_DATA_BUFFER_HEADER_SIZE 16
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE 255

#define UNICODE_NULL ((WCHAR)0)

#define FIELD_OFFSET(type, field) (ULONG)(((unsigned long long)&((type*)0)->field))

#define PAGE_SHIFT 12L
#define PAGE_SIZE 4096

#define REG_DWORD 1
#define REG_QWORD 1

#define SECURITY_NT_AUTHORITY {0x2, 0x3, 0x4, 0x5, 0x6, 0x7}

#define ACL_REVISION 1
#define ACCESS_ALLOWED_ACE_TYPE 2
#define SECURITY_DESCRIPTOR_REVISION 1

#define OWNER_SECURITY_INFORMATION (1<<0)
#define GROUP_SECURITY_INFORMATION (1<<0)
#define DACL_SECURITY_INFORMATION (1<<0)
#define SACL_SECURITY_INFORMATION (1<<0)

#define FILE_WRITE_TO_END_OF_FILE 0xFFFFFFFF

#define SE_LOAD_DRIVER_PRIVILEGE 10

#define INFINITE 0xFFFFFFFF

#define NTSYSCALLAPI

#define HMODULE HANDLE

#define WINAPI

#define CP_UTF8 1

#define TOKEN_ADJUST_PRIVILEGES 1
#define TOKEN_QUERY 2

#define SE_PRIVILEGE_ENABLED 1

#define APIENTRY

#define DLL_PROCESS_ATTACH 1

static const ULONG LowPagePriority = 1;

BOOLEAN FsRtlAreThereCurrentFileLocks(FILE_LOCK* lock);
LONG KeSetEvent(PKEVENT event, int a, BOOLEAN b);

PVOID ExAllocatePoolWithTag(POOL_TYPE PoolType, size_t size, unsigned int tag);

void DbgPrint(const char* msg, ...);

NTSTATUS RtlUnicodeStringPrintf(PUNICODE_STRING out, WCHAR* format, ...);

NTSTATUS RtlUpcaseUnicodeString(PUNICODE_STRING Dest, PUNICODE_STRING Source, BOOLEAN Allocate);

PETHREAD PsGetCurrentThread();

NTSTATUS RtlStringCbVPrintfA(char* dest, size_t dest_size, const char* format, va_list argList);

NTSTATUS RtlUpperString(STRING* Dest, STRING* Source);

void RtlZeroMemory(void* buf, size_t len);

void KeInitializeEvent(PKEVENT Event, EVENT_TYPE Type, BOOLEAN State);

PIRP IoAllocateIrp(const char StackSize, BOOLEAN ChargeQuota);

PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP Irp);

PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp);

PMDL IoAllocateMdl(PVOID VirtualAddress, ULONG Length, BOOLEAN SecondaryBuffer, BOOLEAN ChargeQuota, PIRP Irp);

void MmBuildMdlForNonPagedPool(PMDL Mdl);

NTSTATUS KeWaitForSingleObject(PVOID Object, KWAIT_REASON WaitReason, REQMODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Timeout);

void IoSetCompletionRoutine(PIRP Irp, PIO_COMPLETION_ROUTINE CompletionRoutine, PVOID Context, BOOLEAN InvokeOnSucces, BOOLEAN InvokeOnError, BOOLEAN InvokeOnCancel);

NTSTATUS IoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp);

void IoFreeMdl(PMDL mdl);

void IoFreeIrp(PIRP Irp);

NTSTATUS ZwWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);

void ExFreePool(PVOID buf);

PIRP IoGetTopLevelIrp();

void IoSetTopLevelIrp(PIRP Irp);

NTSTATUS IoDeleteSymbolicLink(PUNICODE_STRING Path);

NTSTATUS IoDeleteDevice(PDEVICE_OBJECT DeviceObject);

BOOLEAN IsListEmpty(const LIST_ENTRY* ListHead);

PLIST_ENTRY RemoveHeadList(PLIST_ENTRY ListHead);

FORCEINLINE void InitializeListHead(PLIST_ENTRY ListHead)
{
	ListHead->Flink = ListHead;
	ListHead->Blink = ListHead;
}

FORCEINLINE void InsertTailList(PLIST_ENTRY ListHead, PLIST_ENTRY ListEntry)
{
	PLIST_ENTRY LastEntry = ListHead->Blink;

	ListEntry->Flink = ListHead;
	ListEntry->Blink = LastEntry;
	ListHead->Blink = ListEntry;
	LastEntry->Flink = ListEntry;
}

PLIST_ENTRY RemoveTailList(PLIST_ENTRY ListHead);

BOOLEAN RemoveEntryList(PLIST_ENTRY ListEntry);

void InsertHeadList(PLIST_ENTRY ListHead, PLIST_ENTRY ListEntry);

void ObDereferenceObject(PVOID FileObject);

void ZwClose(HANDLE hFile);

size_t RtlCompareMemory(const void* const src1, const void* const src2, size_t len);

FORCEINLINE void RtlCopyMemory(PVOID dst, const void* src, size_t len)
{
    memcpy(dst, src, len);
}

void RtlMoveMemory(PVOID dst, const void* src, size_t len);

void FsRtlEnterFileSystem();

void FsRtlExitFileSystem();

void IoCompleteRequest(PIRP Irp, char PriorityBoost);

NTSTATUS FsRtlCheckOplock(POPLOCK Oplock, PVOID CallbackData, PVOID Context, PVOID WaitCompletionRoutine, PVOID PrePostCallbackDataRoutine);

void CcFlushCache(PSECTION_OBJECT_POINTERS SectionObjectPointers, PLARGE_INTEGER FileOffset, ULONG Length, PIO_STATUS_BLOCK IoStatus);

KAFFINITY KeQueryActiveProcessors();

NTSTATUS ZwQueryInformationProcess(HANDLE ProcessHandle, PROCESSINFOCLASS InformationType, PVOID info, ULONG info_length, PULONG ret_len);

HANDLE NtCurrentProcess();

BOOLEAN FsRtlAreNamesEqual(PUNICODE_STRING name1, PUNICODE_STRING name2, BOOLEAN ignore_case, PVOID UpcaseTable);

ULONG RtlWalkFrameChain(void** chain, ULONG n1, ULONG n2);

NTSTATUS FsRtlNotifyVolumeEvent(PFILE_OBJECT Vol, ULONG Action);

PFILE_OBJECT FsRtlNotifyGetLastVolumeEventsubject();

NTSTATUS FsRtlNotifyFilterReportChange(PNOTIFY_SYNC NotifySync, PLIST_ENTRY DirNotifyList, PSTRING fn, USHORT name_offset, PSTRING stream, PVOID arg1, ULONG filter_match, ULONG action, PVOID arg2, PVOID arg3);

LONG InterlockedIncrement(LONG volatile* val);

LONGLONG InterlockedIncrement64(LONGLONG volatile* val);

LONG InterlockedDecrement(LONG volatile* val);

void ExFreeToNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside, PVOID Entry);

void ExFreeToPagedLookasideList(PPAGED_LOOKASIDE_LIST Lookaside, PVOID Entry);

void ExDeletePagedLookasideList(PPAGED_LOOKASIDE_LIST Lookaside);
void ExDeleteNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside);

void FsRtlUninitializeFileLock(PFILE_LOCK FileLock);

void FsRtlUninitializeOplock(POPLOCK Oplock);

void RtlFreeUnicodeString(PUNICODE_STRING String);

BOOLEAN CcUninitializeCacheMap(PFILE_OBJECT FileObject, PVOID arg1, PVOID arg2);

void IoAcquireVpbSpinLock(PKIRQL Irql);

void IoReleaseVpbSpinLock(KIRQL Irql);

void KeSetTimer(PKTIMER Timer, LARGE_INTEGER Time, PVOID arg1);

void IoDetachDevice(PDEVICE_OBJECT DeviceObject);

void CcSetFileSizes(PFILE_OBJECT FileObject, PCC_FILE_SIZES FileSizes);

NTSTATUS GetExceptionCode();

void KeQuerySystemTime(PLARGE_INTEGER Time);

void IoRemoveShareAccess(PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess);

PEPROCESS IoGetRequestorProcess(PIRP Irp);

NTSTATUS FsRtlFastUnlockAll(PFILE_LOCK FileLock, PFILE_OBJECT FileObject, PEPROCESS Requestor, PVOID arg1);

void FsRtlNotifyCleanup(PNOTIFY_SYNC NotifySync, PLIST_ENTRY NotifyList, PVOID arg1);

void CcPurgeCacheSection(PSECTION_OBJECT_POINTERS SectionPointers, PLARGE_INTEGER FileOffset, ULONG Length, ULONG Flags);

void MmProbeAndLockPages(PMDL Mdl, REQMODE Reqmode, ACCESS_MODE AccessMode);

void MmUnlockPages(PMDL Mdl);

PIRP IoBuildDeviceIoControlRequest(ULONG ControlCode, PDEVICE_OBJECT DeviceObject, PVOID InputBuffer, ULONG InputBufferSize, PVOID OutputBuffer, ULONG OutputBufferSize, BOOLEAN Internal, PKEVENT Event, PIO_STATUS_BLOCK IoStatusBlock);

void CcInitializeCacheMap(PFILE_OBJECT FileObject, PCC_FILE_SIZES CcFileSizes, BOOLEAN PinAccess, PCACHE_MANAGER_CALLBACKS Callbacks, PVOID Context);

void CcSetReadAheadGranularity(PFILE_OBJECT FileObject, ULONG Size);

void KeInitializeSpinLock(PKSPIN_LOCK Spinlock);

void InitializeObjectAttributes(POBJECT_ATTRIBUTES ObjectAttributes, PUNICODE_STRING arg1, OBJECT_ATTRIBUTE_TYPE Type, PVOID arg2, PVOID arg3);

NTSTATUS PsCreateSystemThread(HANDLE* hThread, ULONG access, POBJECT_ATTRIBUTES attr, HANDLE process, PVOID arg1, PKSTART_ROUTINE Routine, PVOID Context);

NTSTATUS IoGetDeviceInterfaces(PGUID arg1, PVOID arg2, ULONG len, WCHAR** list);

NTSTATUS IoRegisterDeviceObjectPointer(PUNICODE_STRING Name, PFILE_OBJECT FileObject, PDEVICE_OBJECT DeviceObject);

NTSTATUS IoGetDeviceObjectPointer(PUNICODE_STRING Name, ULONG ObjectPointerType, PFILE_OBJECT* FileObject, PDEVICE_OBJECT* DeviceObject);

void ObReferenceObject(PVOID Obj);

PDEVICE_OBJECT IoGetLowerDeviceObject(PDEVICE_OBJECT DeviceObject);

NTSTATUS IoCreateDevice(PDRIVER_OBJECT DriverObject, ULONG Length, PUNICODE_STRING Name, DEVICE_TYPE Type, ULONG Characteristics, BOOLEAN Exclusive, PDEVICE_OBJECT* DeviceObject);

BOOLEAN IoIsErrorUserInduced(NTSTATUS Status);

void IoSetHardErrorOrVerifyDevice(PIRP Irp, PDEVICE_OBJECT DeviceObject);

void ExInitializeFastMutex(PFAST_MUTEX FastMutex);

void FsRtlNotifyInitializeSync(PNOTIFY_SYNC* NotifySync);

void ExInitializePagedLookasideList(PPAGED_LOOKASIDE_LIST LookasideList, PVOID arg1, PVOID arg2, ULONG Flags, SIZE_T Size, ULONG Tag, USHORT Depth);

void ExInitializeNPagedLookasideList(PNPAGED_LOOKASIDE_LIST LookasideList, PVOID arg1, PVOID arg2, ULONG Flags, SIZE_T Size, ULONG Tag, USHORT Depth);

void IoRaiseInformationalHardError(NTSTATUS Status, PUNICODE_STRING Msg, PKTHREAD Thread);

PFILE_OBJECT IoCreateStreamFileObject(PVOID arg1, PDEVICE_OBJECT DeviceObject);

NTSTATUS FsRtlProcessFileLock(PFILE_LOCK FileLock, PIRP Irp, PVOID arg1);

void RtlInitUnicodeString(PUNICODE_STRING InitStr, WCHAR* Str);

void IoUnregisterFileSystem(PDEVICE_OBJECT DeviceObject);

void IoUnregisterPlugPlayNotification(PVOID Entry);
void PoStartNextPowerIrp(PIRP Irp);

void IoSkipCurrentIrpStackLocation(PIRP Irp);

NTSTATUS PoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp);

PVOID ExAllocateFromNPagedLookasideList(PNPAGED_LOOKASIDE_LIST LookasideList);

PVOID ExAllocateFromPagedLookasideList(PPAGED_LOOKASIDE_LIST LookasideList);

void KeClearEvent(PKEVENT Event);

void KeInitializeTimer(PKTIMER Timer);

void KeCancelTimer(PKTIMER Timer);

void PsTerminateSystemThread(NTSTATUS Status);

NTSTATUS ZwCreateFile(HANDLE* pHandle, ULONG PointerType, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PVOID arg1, ULONG Attributes, ULONG ShareMode, ULONG OpenMode, ULONG Flags, PVOID arg2, ULONG arg3);

NTSTATUS ZwQueryInformationFile(HANDLE Handle, PIO_STATUS_BLOCK IoStatusBlock, PVOID Out, ULONG OutLen, FILE_INFORMATION_CLASS InformationType);

NTSTATUS ZwSetInformationFile(HANDLE Handle, PIO_STATUS_BLOCK IoStatusBlock, PVOID In, ULONG InLen, FILE_INFORMATION_CLASS InformationType);

void RtlTimeToTimeFields(PLARGE_INTEGER Time, PTIME_FIELDS TimeFields);

NTSTATUS IoCreateSymbolicLink(PUNICODE_STRING Name, PUNICODE_STRING Target);

NTSTATUS IoRegisterDeviceInterface(PDEVICE_OBJECT DeviceObject, PGUID Guid, PVOID arg1, PUNICODE_STRING Name);

PDEVICE_OBJECT IoAttachDeviceToDeviceStack(PDEVICE_OBJECT DeviceObject, PDEVICE_OBJECT AttachDeviceObject);

NTSTATUS IoSetDeviceInterfaceState(PUNICODE_STRING Name, BOOLEAN set);

PVOID MmGetSystemRoutineAddress(PUNICODE_STRING Name);

NTSTATUS ZwCreateKey(HANDLE* hKey, ULONG Action, POBJECT_ATTRIBUTES ObjectAttributes, ULONG arg1, PVOID arg2, ULONG Flags, PULONG Disposition);

NTSTATUS IoReportDetectedDevice(PDRIVER_OBJECT DriverObject, REPORT_INTERFACE_TYPE InterfaceType, ULONG arg1, ULONG arg2, PVOID arg3, PVOID arg4, ULONG arg5, PDEVICE_OBJECT* DeviceObject);

void IoInvalidateDeviceRelations(PDEVICE_OBJECT DeviceObject, DEVICE_RELATIONS_TYPE DeviceRelation);

NTSTATUS IoRegisterPlugPlayNotification(PLUG_N_PLAY_EVENT_CATEGORIES EventCategory, ULONG Flags, PVOID arg1, PDRIVER_OBJECT DriverObject1, PNPNOTIFICATION_FUN NotificationFun, PVOID Context, PVOID arg2);

void IoRegisterFileSystem(PDEVICE_OBJECT DeviceObject);

void IoRegisterBootDriverReinitialization(PDRIVER_OBJECT DriverObject, BOOTDRIVERINIT_FUN InitFun, PVOID Context);

LUID RtlConvertLongToLuid(LONG Long);

BOOLEAN SeSinglePrivilegeCheck(LUID Lid, KPROCESSOR_MODE processor_mode);

void RtlInitializeBitMap(PRTL_BITMAP Bitmap, PULONG Buffer, ULONG Size);

void RtlSetAllBits(PRTL_BITMAP Bitmap);

void RtlClearBits(PRTL_BITMAP Bitmap, ULONG Start, ULONG Len);

BOOLEAN RtlCheckBit(PRTL_BITMAP Bitmap, ULONG Off);

BOOLEAN RtlAreBitsClear(PRTL_BITMAP Bitmap, ULONG Start, ULONG Len);

void RtlSetBit(PRTL_BITMAP Bitmap, ULONG Off);

void RtlClearAllBits(PRTL_BITMAP Bitmap);

void RtlSetBits(PRTL_BITMAP Bitmap, ULONG Start, ULONG Len);

ULONG RtlFindNextForwardRunClear(PRTL_BITMAP Bitmap, ULONG Off, PULONG Start);

ULONG RtlFindFirstRunClear(PRTL_BITMAP Bitmap, PULONG Start);

NTSTATUS ZwOpenSymbolicLinkObject(HANDLE hObj, ULONG OpenMode, POBJECT_ATTRIBUTES Attrs);

NTSTATUS ZwQuerySymbolicLinkObject(HANDLE hObj, PUNICODE_STRING Target, PULONG retlen);

void NtClose(HANDLE hObj);

NTSTATUS ZwQueryValueKey(HANDLE hObj, PUNICODE_STRING Name, QUERY_VALUE_TYPE Type, PKEY_VALUE_FULL_INFORMATION KvInf, ULONG KvInfLen, PULONG KvInfLenOut);

PKTHREAD KeGetCurrentThread();

void KeAcquireSpinLock(PKSPIN_LOCK Spinlock, PKIRQL Irql);
void KeReleaseSpinLock(PKSPIN_LOCK Spinlock, KIRQL Irql);

void KeSetSystemAffinityThread(KAFFINITY Affinity);

void FsRtlSetupAdvancedHeader(PFSRTL_ADVANCED_FCB_HEADER Header, PFAST_MUTEX FastMutex);

void FsRtlInitializeFileLock(PFILE_LOCK FileLock, PVOID arg1, PVOID arg2);

void FsRtlInitializeOplock(POPLOCK Oplock);

NTSTATUS IoCheckEaBufferValidity(PFILE_FULL_EA_INFORMATION EaInformation, ULONG Length, PULONG Offset);

BOOLEAN RtlValidRelativeSecurityDescriptor(PVOID SecurityDescriptor, ULONG Length, PVOID arg1);

void SeLockSubjectContext(PSECURITY_SUBJECT_CONTEXT SecSubjContext);

BOOLEAN SeAccessCheck(PSECURITY_DESCRIPTOR Sd, PSECURITY_SUBJECT_CONTEXT SecSubjContext, BOOLEAN b, ULONG FileOption, ULONG arg1, PVOID arg2, PGENERIC_MAPPING GenMap, REQMODE Reqmode, ACCESS_MASK* Access, NTSTATUS* status);

PGENERIC_MAPPING IoGetFileObjectGenericMapping();

void IoSetShareAccess(ACCESS_MASK Access, ULONG DesiredAccess, PFILE_OBJECT FileObj, PSHARE_ACCESS ShareAccess);

void SeUnlockSubjectContext(PSECURITY_SUBJECT_CONTEXT SecSubjContext);

BOOLEAN MmFlushImageSection(PSECTION_OBJECT_POINTERS SectionPointers, FLUSH_MODE FlushMode);

BOOLEAN MmCanFileBeTruncated(PSECTION_OBJECT_POINTERS SectionPointers, PLARGE_INTEGER Size);

NTSTATUS IoCheckShareAccess(ACCESS_MASK AccessMask, ULONG Granted, PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess, BOOLEAN b1);

void IoUpdateShareAccess(PFILE_OBJECT, PSHARE_ACCESS ShareAccess);

PDEVICE_OBJECT IoGetDeviceToVerify(PETHREAD Thread);

void IoSetDeviceToVerify(PETHREAD Thread, PDEVICE_OBJECT DevObj);

NTSTATUS IoVerifyVolume(PDEVICE_OBJECT DevObj, BOOLEAN verify);

BOOLEAN SePrivilegeCheck(PPRIVILEGE_SET PrivilegeSet, PSECURITY_SUBJECT_CONTEXT SecSubjContext, KPROCESSOR_MODE ProcessorMode);

BOOLEAN FsRtlDoesNameContainWildCards(PUNICODE_STRING Str);

BOOLEAN FsRtlIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PVOID arg1);

void FsRtlNotifyFilterChangeDirectory(PNOTIFY_SYNC NotifySync, PLIST_ENTRY DirList, PVOID FsContext, PSTRING Filename,
    ULONG Flags, BOOLEAN b1, ULONG CompletionFilter, PIRP Irp, PVOID arg1, PVOID arg2, PVOID arg3);

void FsRtlNotifyFullChangeDirectory(PNOTIFY_SYNC NotifySync, PLIST_ENTRY DirList, PVOID FsContext, PVOID arg1,
    BOOLEAN arg2, BOOLEAN arg3, ULONG arg4, PVOID arg5, PVOID arg6, PVOID arg7);

BOOLEAN FsRtlCopyRead(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, ULONG LockKey,
    PVOID Buf, PIO_STATUS_BLOCK Status, PDEVICE_OBJECT DeviceObject);

BOOLEAN FsRtlMdlReadDev(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, ULONG LockKey, PMDL* Chain,
    PIO_STATUS_BLOCK Status, PDEVICE_OBJECT DeviceObject);

BOOLEAN FsRtlMdlReadCompleteDev(PFILE_OBJECT FileObject, PMDL Chain, PDEVICE_OBJECT DeviceObject);

BOOLEAN FsRtlPrepareMdlWriteDev(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, ULONG LockKey, PMDL* Chain,
    PIO_STATUS_BLOCK Status, PDEVICE_OBJECT DeviceObject);

BOOLEAN FsRtlMdlWriteCompleteDev(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, PMDL Chain, PDEVICE_OBJECT DeviceObject);

BOOLEAN FsRtlFastCheckLockForRead(PFILE_LOCK FileLock, PLARGE_INTEGER Offset, PLARGE_INTEGER Len, ULONG Key, PFILE_OBJECT FileObj,
    PVOID ProcId);

BOOLEAN FsRtlFastCheckLockForWrite(PFILE_LOCK FileLock, PLARGE_INTEGER Offset, PLARGE_INTEGER Len, ULONG Key, PFILE_OBJECT FileObj,
    PVOID ProcId);

BOOLEAN FsRtlCopyWrite(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, ULONG LockKey,
    PVOID Buf, PIO_STATUS_BLOCK Status, PDEVICE_OBJECT DeviceObject);

BOOLEAN FsRtlFastLock(PFILE_LOCK FileLock, PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, PLARGE_INTEGER Len,
    PEPROCESS ProcID, ULONG Key, BOOLEAN FailImmediately, BOOLEAN ExclusiveLock, PIO_STATUS_BLOCK Status, PVOID arg1, BOOLEAN arg2);

NTSTATUS FsRtlFastUnlockSingle(PFILE_LOCK FileLock, PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, PLARGE_INTEGER Len,
    PEPROCESS ProcID, ULONG Key, PVOID arg1, BOOLEAN arg2);

NTSTATUS FsRtlFastUnlockAllByKey(PFILE_LOCK FileLock, PFILE_OBJECT FileObject, 
    PEPROCESS ProcID, ULONG Key, PVOID arg1);

ULONG RtlLengthSecurityDescriptor(PSECURITY_DESCRIPTOR SecurityDescriptor);

void SeCaptureSubjectContext(PSECURITY_SUBJECT_CONTEXT SecurityContext);

void SeReleaseSubjectContext(PSECURITY_SUBJECT_CONTEXT SecurityContext);

NTSTATUS SeAssignSecurity(PSECURITY_DESCRIPTOR SecurityDescriptor,
    PVOID arg1, PVOID* SdLink, BOOLEAN arg2, PSECURITY_SUBJECT_CONTEXT SecurityContext,
    PGENERIC_MAPPING Mapping, POOL_TYPE PoolType);

NTSTATUS RtlGetOwnerSecurityDescriptor(PSECURITY_DESCRIPTOR SecurityDescriptor,
    PSID* Owner, BOOLEAN* defaulted);

LARGE_INTEGER KeQueryPerformanceCounter(PLARGE_INTEGER arg1);

ULONG RtlRandomEx(PULONG Prev);

BOOLEAN IoIs32bitProcess(PIRP Irp);

HANDLE Handle32ToHandle(void* POINTER_32 ptr);

HANDLE ObRegisterHandle(PVOID Object);

NTSTATUS ObDeregisterHandle(HANDLE Handle);

NTSTATUS ObReferenceObjectByHandle(HANDLE Handle, ACCESS_MASK Access,
    POBJECT_TYPE ObjectType, KPROCESSOR_MODE ProcMode, PVOID* Object, PVOID arg1);

BOOLEAN KeReadStateEvent(PKEVENT Event);

HANDLE LongToHandle(ULONG tLong);

void CcWaitForCurrentLazyWriterActivity();

NTSTATUS SeAssignSecurityEx(PSECURITY_DESCRIPTOR SecurityDescriptor, PVOID arg1, PVOID* Sd, PVOID arg2,
    BOOLEAN Directory, ULONG Flags, PSECURITY_SUBJECT_CONTEXT SecSubjContext, PGENERIC_MAPPING Mapping, POOL_TYPE PoolType);

NTSTATUS FsRtlOplockFsctrl(POPLOCK Oplock, PIRP Irp, ULONG Count);

BOOLEAN IsReparseTagMicrosoft(ULONG ReparseTag);

void IoAdjustPagingPathCount(LONG* Count, BOOL b1);

PIRP IoMakeAssociatedIrp(PIRP Irp, char StackSize);

void CcMdlRead(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, PMDL* Address, PIO_STATUS_BLOCK Status);

void IoMarkIrpPending(PIRP Irp);

BOOLEAN CcCopyRead(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, PUCHAR Data, PIO_STATUS_BLOCK Status);

BOOLEAN IoIsSystemThread(PETHREAD Thread);

PEPROCESS PsGetThreadProcess(PETHREAD Thread);

//void fPsUpdateDiskCounters(PEPROCESS Process, ULONG Num, ULONG arg1, ULONG arg2, ULONG arg3, ULONG arg4);

void CcMdlReadComplete(PFILE_OBJECT FileObject, PMDL Mdl);

BOOLEAN IoIsOperationSynchronous(PIRP Irp);

BOOLEAN FsRtlCheckLockForReadAccess(PFILE_LOCK Lock, PIRP Irp);

NTSTATUS ZwEnumerateValueKey(HANDLE h, ULONG index, REG_ENUM_TYPE EnumyType, PVOID Out, ULONG OutLen, PULONG retlen);

NTSTATUS ZwOpenKey(PHANDLE h, ULONG Access, POBJECT_ATTRIBUTES Attr);

NTSTATUS ZwSetValueKey(HANDLE h, PUNICODE_STRING Path, ULONG Idx, ULONG Type, PVOID Data, ULONG DataLen);

NTSTATUS ZwDeleteKey(HANDLE h);

NTSTATUS ZwEnumerateKey(HANDLE h, ULONG Idx, REG_ENUM_TYPE EnumType, PVOID Out, ULONG OutLen, PULONG retlen);

NTSTATUS ZwDeleteValueKey(HANDLE h, PUNICODE_STRING Path);

NTSTATUS ZwNotifyChangeKey(HANDLE h, PVOID arg1, PVOID arg2, PVOID arg3, PIO_STATUS_BLOCK IoSb, ULONG Action, BOOLEAN arg4,
    PVOID arg5, ULONG arg6, BOOLEAN arg7);

typedef void(*WQFUN)(PVOID arg);

void ExInitializeWorkItem(PWORK_QUEUE_ITEM Wqi, WQFUN Fun, HANDLE h);

NTSTATUS SeQueryInformationToken(PACCESS_TOKEN Token, QUERY_INFORMATION_TOKEN_TYPE TokenType, PVOID Out);

ULONG RtlLengthSid(PSID Sid);

BOOLEAN RtlEqualSid(PSID SidA, PSID SidB);

NTSTATUS RtlCreateSecurityDescriptor(PSECURITY_DESCRIPTOR Sd, ULONG SdRevision);

NTSTATUS RtlSetOwnerSecurityDescriptor(PSECURITY_DESCRIPTOR Sd, PSID Sid, BOOLEAN arg1);

NTSTATUS RtlSetGroupSecurityDescriptor(PSECURITY_DESCRIPTOR Sd, PSID Sid, BOOLEAN arg1);

NTSTATUS RtlSetDaclSecurityDescriptor(PSECURITY_DESCRIPTOR Sd, BOOLEAN ar1, PACL Acl, BOOLEAN arg2);

NTSTATUS RtlAbsoluteToSelfRelativeSD(PSECURITY_DESCRIPTOR Sd, PVOID arg1, PULONG len);

NTSTATUS RtlSelfRelativeToAbsoluteSD(PSECURITY_DESCRIPTOR Sd, PVOID arg1, PULONG Abslen, PVOID arg2,
    PULONG DaclLen, PVOID arg3, PULONG SaclLen, PVOID arg4, PULONG OwnerLen, PVOID arg5, PULONG GroupLen);

NTSTATUS SeQuerySecurityDescriptorInfo(PSECURITY_INFORMATION Flags, PSECURITY_DESCRIPTOR Sd,
    PULONG BufLen, PVOID* SdOut);

NTSTATUS SeSetSecurityDescriptorInfo(PVOID arg1, PSECURITY_INFORMATION Flags, PSECURITY_DESCRIPTOR Sd,
    PVOID* SdOut, POOL_TYPE PoolType, PGENERIC_MAPPING Mapping);

void ExAcquireFastMutex(PFAST_MUTEX Mutex);

void ExReleaseFastMutex(PFAST_MUTEX Mutex);

PVOID MmGetMdlVirtualAddress(PMDL Mdl);

void IoBuildPartialMdl(PMDL MdlAdress, PMDL Mdl, PVOID Ptr, ULONG Len);

void IoCancelIrp(PIRP Irp);

BOOLEAN CcCanIWrite(PFILE_OBJECT FileObject, ULONG Length, BOOLEAN Wait, BOOLEAN Deferred);

void CcPrepareMdlWrite(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, PMDL* Mdl, PIO_STATUS_BLOCK Status);

BOOLEAN CcCopyWrite(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, PVOID Buffer);

BOOLEAN FsRtlCheckLockForWriteAccess(PFILE_LOCK FileLock, PIRP Irp);

void CcMdlWriteComplete(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, PMDL Mdl);

BOOLEAN WdmlibRtlIsNtDdiVersionAvailable(ULONG Version);

void init_windef();

static HANDLE GetCurrentProcess(void) { return 0; }

BOOL OpenProcessToken(HANDLE hProc, ULONG Flags, PHANDLE Token);

BOOL LookupPrivilegeValueW(PVOID ptr1, WCHAR* Name, PLUID Luid);

static void GetSystemTimeAsFileTime(FILETIME* Filetime) {
    Filetime->dwLowDateTime = 0;
    Filetime->dwHighDateTime = 0;
}

ULONG WideCharToMultiByteInt(ULONG Codepage, DWORD Flags, WCHAR* Wchar, int NumWchar, char* buf, ULONG blen, PVOID ptr1, PVOID ptr2);

static ULONG WideCharToMultiByte(ULONG Codepage, DWORD Flags, WCHAR* Wchar, int NumWchar, char* buf, ULONG blen, PVOID ptr1, PVOID ptr2)
{
    return WideCharToMultiByteInt(Codepage, Flags, Wchar, NumWchar, buf, blen, ptr1, ptr2);
}

NTSTATUS NtDeviceIoControlFile(HANDLE h, PVOID ptr1, PVOID ptr2, PVOID ptr3, PIO_STATUS_BLOCK Iocb, ULONG Cmd,
    PVOID Apte1, ULONG Ape1Len, PVOID Apte2, ULONG Ape2Len);

void RegisterNtFile(WCHAR* Name, PVOID file);

void UnregisterNtFile(WCHAR* Name);

NTSTATUS NtOpenFile(PHANDLE Handle, ULONG Attributes, POBJECT_ATTRIBUTES Atts, PIO_STATUS_BLOCK iosb,
    ULONG SharingMode, ULONG AlertMode);

NTSTATUS NtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer,
    ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);

NTSTATUS NtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer,
    ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);

static void CloseHandle(HANDLE h) {}

BOOL AdjustTokenPrivileges(HANDLE token, BOOL b, TOKEN_PRIVILEGES* TokenPrivs, ULONG TokenPrivsSize, PVOID ptr1, PVOID ptr2);

NTSTATUS NtFsControlFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, 
    PIO_STATUS_BLOCK IoStatusBlock, ULONG FsControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);

#ifndef _WIN32
int MultiByteToWideChar(UINT CodePage, DWORD Flags, char* ByteStr, int ByteStrSize, wchar_t* OutStr, int OutStrSize);
#define CP_OEMCP 1
#define MB_PRECOMPOSED 1

PVOID GetModuleHandle(PVOID p1);
BOOL LoadStringW(PVOID module, int resid, WCHAR* str, size_t str_size);
DWORD GetLastError(void);

HMODULE LoadLibraryW(WCHAR* str);
PVOID GetProcAddress(HMODULE, char* name);
#endif
   
#define CONTAINING_RECORD(addr, type, field) ((type*)( (char*)(addr)-(ULONG_PTR)(&((type*)0)->field)))
#define FILE_ATTRIBUTE_SPARSE_FILE (1)
#define NT_SUCCESS(x) ((x)==STATUS_SUCCESS)
#ifndef min
#define min(x,y) (((x)<(y)) ? (x) : (y))
#endif

#ifndef max
#define max(x,y) (((x)>(y)) ? (x) : (y))
#endif

#include "resource.h"
#include "workqueue.h"