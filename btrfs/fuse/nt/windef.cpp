extern "C"
{
#include "windef.h"
#include "guiddef.h"
#include "device.h"
}
#include "../fuse.h"

#include <memory>
#include <iostream>
#include <thread>
#include <stdarg.h>
#include <assert.h>
#include "../oslib/os.h"
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <set>
#include <array>
#include <thread>
#include <condition_variable>
#include "file.h"
#include "../oslib/utf8.h"
#include "../../../utf8/utf8.h"

#ifndef _WIN32
#define vsprintf_s vsnprintf
#define vprintf_s vprintf
#define vswprintf_s vswprintf
#endif

POBJECT_TYPE* IoFileObjectType;

namespace
{
	struct SRegisteredDeviceObject
	{
		PDEVICE_OBJECT DeviceObject;
		PFILE_OBJECT FileObject;
	};

	std::map<std::wstring, SRegisteredDeviceObject> registered_device_objects;

	struct SEventSlot
	{
		std::mutex m;
		HANDLE evt;
		HANDLE evt_a;
		HANDLE evt_b;
		unsigned int* waiters;
		unsigned int waiters_a;
		unsigned int waiters_b;
		std::condition_variable wait_c;
	};

	std::array<SEventSlot, 100> event_slots;
	std::atomic<unsigned int> curr_event_slot(0);


	const unsigned short handle_tt_file = 1;
	const unsigned short handle_tt_thread = 2;
	const unsigned short handle_tt_registered = 3;

	HANDLE addTypeTag(HANDLE h, unsigned short tt)
	{
		return reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(h) | (tt & 0xF));
	}

	HANDLE removeTypeTag(HANDLE h, unsigned short& tt)
	{
		tt = reinterpret_cast<ULONG_PTR>(h) & 0xF;
		return reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(h) & ~0xF);
	}

	PFILE_OBJECT last_file_object = NULL;
}

struct KSPIN_LOCK_IMPL
{
	std::mutex m;
};

LONG KeSetEvent(PKEVENT event, int a, BOOLEAN b)
{
	LONG prev = std::atomic_load_explicit(reinterpret_cast<std::atomic<LONG>*>(&(event->eword)), std::memory_order_acquire);
	if (prev == 0)
	{
		std::atomic_store_explicit(reinterpret_cast<std::atomic<LONG>*>(&(event->eword)), 1, std::memory_order_release);
		os_wake_by_address_single(&(event->eword));
	}
	return prev;
}

void KeInitializeEvent(PKEVENT Event, EVENT_TYPE Type, BOOLEAN State)
{
	Event->eword = State ? 1 : 0;
}

NTSTATUS KeWaitForSingleObject(PVOID Object, KWAIT_REASON WaitReason, REQMODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Timeout)
{
	PKEVENT Event = reinterpret_cast<PKEVENT>(Object);
	if (std::atomic_load_explicit(reinterpret_cast<std::atomic<LONG>*>(&(Event->eword)), std::memory_order_relaxed) != 0)
	{
		return STATUS_SUCCESS;
	}

	LONG curr_eword = 0;
	do
	{
		os_wait_on_address(&(Event->eword), &curr_eword, sizeof(curr_eword), INFINITE);
	} while (std::atomic_load_explicit(reinterpret_cast<std::atomic<LONG>*>(&(Event->eword)), std::memory_order_acquire) == 0);

	return STATUS_SUCCESS;
}

BOOLEAN KeReadStateEvent(PKEVENT Event)
{
	return std::atomic_load_explicit(reinterpret_cast<std::atomic<LONG>*>(&(Event->eword)), std::memory_order_relaxed) > 0;
}

void KeClearEvent(PKEVENT Event)
{
	std::atomic_store_explicit(reinterpret_cast<std::atomic<LONG>*>(&(Event->eword)), 0, std::memory_order_release);
}

void init_object_type()
{
	IoFileObjectType = new POBJECT_TYPE;
}

void init_windef()
{
	init_object_type();
}

BOOL OpenProcessToken(HANDLE hProc, ULONG Flags, PHANDLE Token)
{
	return TRUE;
}

BOOL LookupPrivilegeValueW(PVOID ptr1, WCHAR* Name, PLUID Luid)
{
	return TRUE;
}

ULONG WideCharToMultiByteInt(ULONG Codepage, DWORD Flags, WCHAR* Wchar, int NumWchar, char* buf, ULONG blen, PVOID ptr1, PVOID ptr2)
{
	if (NumWchar == 0)
	{
		return 0;
	}

	if (NumWchar == -1)
	{
		NumWchar = wcslen(Wchar) + 1;
	}

	std::string ret;
	try
	{
		if (sizeof(wchar_t) == 2)
		{
			utf8::utf16to8(Wchar, Wchar+NumWchar, back_inserter(ret));
		}
		else if (sizeof(wchar_t) == 4)
		{
			utf8::utf32to8(Wchar, Wchar + NumWchar, back_inserter(ret));
		}

	}
	catch (...) {}
	

	if (blen == 0)
	{
		return ret.size();
	}

	ULONG tocopy = (std::min)(blen, static_cast<ULONG>(ret.size()));
	memcpy(buf, ret.data(), tocopy);

	return tocopy;
}

NTSTATUS NtDeviceIoControlFile(HANDLE h, PVOID ptr1, PVOID ptr2, PVOID ptr3, PIO_STATUS_BLOCK Iocb, ULONG Cmd, 
	PVOID Apte1, ULONG Ape1Len, PVOID Apte2, ULONG Ape2Len)
{
	if (Cmd == IOCTL_DISK_GET_LENGTH_INFO)
	{
		IFile* vol = reinterpret_cast<IFile*>(h);
		GET_LENGTH_INFORMATION* gli = reinterpret_cast<GET_LENGTH_INFORMATION*>(Apte2);
		gli->Length.QuadPart = vol->Size();
		return STATUS_SUCCESS;
	}
	else if (Cmd == IOCTL_DISK_GET_DRIVE_GEOMETRY)
	{
		DISK_GEOMETRY* dg = reinterpret_cast<DISK_GEOMETRY*>(Apte2);
		dg->BytesPerSector = 512;
		dg->Cylinders.QuadPart = 0;
		dg->SectorsPerTrack = 1;
		dg->TracksPerCylinder = 1;
		return STATUS_SUCCESS;
	}

	return STATUS_NOT_IMPLEMENTED;
}

std::mutex registered_files_mutex;
std::map<std::wstring, IFile*> registered_files;

void RegisterNtFile(WCHAR* Name, PVOID file)
{
	std::lock_guard<std::mutex> lock(registered_files_mutex);
	registered_files[Name] = reinterpret_cast<IFile*>(file);
}

void UnregisterNtFile(WCHAR* Name)
{
	std::lock_guard<std::mutex> lock(registered_files_mutex);
	auto it = registered_files.find(Name);
	if (it != registered_files.end())
		registered_files.erase(it);
}

NTSTATUS NtOpenFile(PHANDLE Handle, ULONG Attributes, POBJECT_ATTRIBUTES Atts, PIO_STATUS_BLOCK iosb, ULONG SharingMode, ULONG AlertMode)
{
	std::lock_guard<std::mutex> lock(registered_files_mutex);

	auto it = registered_files.find(Atts->path);
	if (it == registered_files.end())
		return STATUS_NOT_FOUND;

	*Handle = it->second;

	return STATUS_SUCCESS;
}

NTSTATUS NtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, 
	PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key)
{
	IFile* f = reinterpret_cast<IFile*>(FileHandle);

	bool has_error = false;
	_u32 rc = f->Write(ByteOffset->QuadPart, reinterpret_cast<char*>(Buffer), Length, &has_error);

	IoStatusBlock->Information = rc;
	IoStatusBlock->Status = STATUS_SUCCESS;

	if (has_error)
	{
		IoStatusBlock->Status = STATUS_CRC_ERROR;
	}

	return IoStatusBlock->Status;
}

NTSTATUS NtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, 
	PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key)
{
	IFile* f = reinterpret_cast<IFile*>(FileHandle);

	bool has_error = false;
	_u32 rc = f->Read(ByteOffset->QuadPart, reinterpret_cast<char*>(Buffer), Length, &has_error);

	IoStatusBlock->Information = rc;
	IoStatusBlock->Status = STATUS_SUCCESS;

	if (has_error)
	{
		IoStatusBlock->Status = STATUS_CRC_ERROR;
	}

	return IoStatusBlock->Status;
}

BOOL AdjustTokenPrivileges(HANDLE token, BOOL b, TOKEN_PRIVILEGES* TokenPrivs, ULONG TokenPrivsSize, PVOID ptr1, PVOID ptr2)
{
	return TRUE;
}

NTSTATUS NtFsControlFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG FsControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength)
{
	return STATUS_SUCCESS;
}

BOOL FsRtlOplockIsFastIoPossible(POPLOCK lock)
{
	return FALSE;
}

PEPROCESS PsGetCurrentProcess()
{
	return PEPROCESS();
}

void* MmGetSystemAddressForMdlSafe(PMDL mdl, ULONG priority)
{
	return mdl->ptr;
}

BOOLEAN FsRtlAreThereCurrentFileLocks(FILE_LOCK* lock)
{
	return FALSE;
}

PVOID ExAllocate(size_t size)
{
	return malloc(size);
}

PVOID ExAllocatePoolWithTag(POOL_TYPE PoolType, size_t size, unsigned int tag)
{
	return malloc(size);
}

void DbgPrint(const char* msg, ...)
{
	va_list arglist;
	va_start(arglist, msg);
	vprintf_s(msg, arglist);
	va_end(arglist);
}

NTSTATUS RtlUnicodeStringPrintf(PUNICODE_STRING out, WCHAR* format, ...)
{
	va_list arglist;
	va_start(arglist, format);
	int rc = vswprintf_s(out->Buffer, out->MaximumLength/sizeof(WCHAR), format, arglist);
	va_end(arglist);

	if (rc == -1)
	{
		return STATUS_INVALID_PARAMETER;
	}
	else
	{
		out->Length = static_cast<USHORT>(rc);
		return STATUS_SUCCESS;
	}
}

NTSTATUS RtlUpcaseUnicodeString(PUNICODE_STRING Dest, PUNICODE_STRING Source, BOOLEAN Allocate)
{
	if (Allocate)
	{
		Dest->Buffer = new WCHAR[Source->Length/sizeof(WCHAR)];
		Dest->Length = Source->Length;
		Dest->MaximumLength = Source->MaximumLength;
	}

	USHORT i;
	for (i = 0; i < Source->Length/sizeof(WCHAR) && i < Dest->MaximumLength/sizeof(WCHAR); ++i)
	{
		WCHAR ch = Source->Buffer[i];
		WCHAR upch = os_toupper(ch);
		Dest->Buffer[i] = upch;
	}

	Dest->Length = i*sizeof(WCHAR);

	return STATUS_SUCCESS;
}

PETHREAD PsGetCurrentThread()
{
	return PETHREAD();
}

NTSTATUS RtlStringCbVPrintfA(char* dest, size_t dest_size, const char* format, va_list argList)
{
	int rc = vsprintf_s(dest, dest_size, format, argList);

	if (rc == -1)
	{
		return STATUS_INVALID_PARAMETER;
	}
	else
	{
		return STATUS_SUCCESS;
	}
}

NTSTATUS RtlUpperString(STRING* Dest, STRING* Source)
{
	USHORT i;
	for (i = 0; i < Source->Length && i < Dest->MaximumLength; ++i)
	{
		WCHAR ch = Source->Buffer[i];
		WCHAR upch = os_toupper(ch);
		Dest->Buffer[i] = static_cast<char>(upch);
	}

	Dest->Length = i;

	return STATUS_SUCCESS;
}

void RtlZeroMemory(void* buf, size_t len)
{
	memset(buf, 0, len);
}

PIRP IoAllocateIrp(const char StackSize, BOOLEAN ChargeQuota)
{
	PIRP ret = new IRP;
	memset(ret, 0, sizeof(IRP));
	return ret;
}

PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP Irp)
{
	return &Irp->StackLocation;
}

PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp)
{
	return &Irp->StackLocation;
}

PMDL IoAllocateMdl(PVOID VirtualAddress, ULONG Length, BOOLEAN SecondaryBuffer, BOOLEAN ChargeQuota, PIRP Irp)
{
	PMDL mdl = new MDL;
	mdl->ptr = VirtualAddress;
	mdl->Length = Length;
	return mdl;
}

void MmBuildMdlForNonPagedPool(PMDL Mdl)
{
}

void IoSetCompletionRoutine(PIRP Irp, PIO_COMPLETION_ROUTINE CompletionRoutine, PVOID Context, BOOLEAN InvokeOnSucces, BOOLEAN InvokeOnError, BOOLEAN InvokeOnCancel)
{
	Irp->CompletionRoutine = CompletionRoutine;
	Irp->CompletionRoutineContext = Context;
	Irp->CompletionRoutineFlags = 0;
	if (InvokeOnSucces)
		Irp->CompletionRoutineFlags |= COMPLETION_ROUTINE_ON_SUCCESS;
	if (InvokeOnError)
		Irp->CompletionRoutineFlags |= COMPLETION_ROUTINE_ON_ERROR;
	if (InvokeOnCancel)
		Irp->CompletionRoutineFlags |= COMPLETION_ROUTINE_ON_CANCEL;
}

NTSTATUS IoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	if (DeviceObject->DiskDevice != NULL)
	{
		return DiskDeviceCall(DeviceObject->DiskDevice, Irp, DeviceObject);
	}
	return NTSTATUS();
}

void IoFreeMdl(PMDL mdl)
{
}

void IoFreeIrp(PIRP Irp)
{
	delete Irp;
}

NTSTATUS ZwWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key)
{
	unsigned short tt;
	FileHandle = removeTypeTag(FileHandle, tt);

	if (tt != handle_tt_file)
		return STATUS_INVALID_PARAMETER;

	File* file = static_cast<File*>(FileHandle);
	bool has_error = false;
	_u32 written = file->Write(static_cast<char*>(Buffer), Length, &has_error);

	if (has_error)
	{
		IoStatusBlock->Status = STATUS_UNEXPECTED_IO_ERROR;
	}

	return STATUS_SUCCESS;
}

void ExFreePool(PVOID buf)
{
	free(buf);
}

PIRP IoGetTopLevelIrp()
{
	return NULL;
}

void IoSetTopLevelIrp(PIRP Irp)
{
}

NTSTATUS IoDeleteSymbolicLink(PUNICODE_STRING Path)
{
	return STATUS_SUCCESS;
}

NTSTATUS IoDeleteDevice(PDEVICE_OBJECT DeviceObject)
{
	return STATUS_SUCCESS;
}

BOOLEAN IsListEmpty(const LIST_ENTRY* ListHead)
{
	return ListHead->Flink == ListHead;
}

PLIST_ENTRY RemoveHeadList(PLIST_ENTRY ListHead)
{
	PLIST_ENTRY ret = ListHead->Flink;
	PLIST_ENTRY next = ListHead->Flink->Flink;
	ListHead->Flink = next;
	next->Blink = ListHead;
	return ret;
}

PLIST_ENTRY RemoveTailList(PLIST_ENTRY ListHead)
{
	PLIST_ENTRY ret = ListHead->Blink;

	ret->Blink->Flink = ListHead;
	ListHead->Blink = ret->Blink;

	return ret;
}

BOOLEAN RemoveEntryList(PLIST_ENTRY ListEntry)
{
	PLIST_ENTRY next = ListEntry->Flink;
	PLIST_ENTRY prev = ListEntry->Blink;

	next->Blink = prev;
	prev->Flink = next;

	return next == prev;
}

void InsertHeadList(PLIST_ENTRY ListHead, PLIST_ENTRY ListEntry)
{
	PLIST_ENTRY next = ListHead->Flink;

	ListHead->Flink = ListEntry;

	ListEntry->Blink = ListHead;
	ListEntry->Flink = next;
	next->Blink = ListEntry;
	ListHead->Flink = ListEntry;
}

void ObDereferenceObject(PVOID FileObject)
{
}

void ZwClose(HANDLE hFile)
{
	if (hFile == NULL)
		return;

	unsigned short tt;
	hFile = removeTypeTag(hFile, tt);

	if (tt == handle_tt_file)
	{
		delete static_cast<File*>(hFile);
	}
	else if (tt == handle_tt_thread)
	{
		delete static_cast<std::thread*>(hFile);
	}
	else
	{
		assert(false);
	}
}

size_t RtlCompareMemory(const void* const src1, const void* const src2, size_t len)
{
	const char* const psrc1 = reinterpret_cast<const char* const>(src1);
	const char* const psrc2 = reinterpret_cast<const char* const>(src2);
	for (size_t i = 0; i < len; ++i)
	{
		if (psrc1[i] != psrc2[i])
		{
			return i;
		}
	}
	return len;
}

void RtlMoveMemory(PVOID dst, const void* src, size_t len)
{
	memmove(dst, src, len);
}

void FsRtlEnterFileSystem()
{
}

void FsRtlExitFileSystem()
{
}

void IoCompleteRequest(PIRP Irp, char PriorityBoost)
{
}

NTSTATUS FsRtlCheckOplock(POPLOCK Oplock, PVOID CallbackData, PVOID Context, PVOID WaitCompletionRoutine, PVOID PrePostCallbackDataRoutine)
{
	return STATUS_SUCCESS;
}

void CcFlushCache(PSECTION_OBJECT_POINTERS SectionObjectPointers, PLARGE_INTEGER FileOffset, ULONG Length, PIO_STATUS_BLOCK IoStatus)
{
}

KAFFINITY KeQueryActiveProcessors()
{
	KAFFINITY ret = 1;

	size_t ncpus = os_get_num_cpus();

	while (ncpus > 1)
	{
		ret *= 2;
		ret += 1;
		ncpus--;
	}

	return ret;
}

HANDLE NtCurrentProcess()
{
	return HANDLE();
}

BOOLEAN FsRtlAreNamesEqual(PUNICODE_STRING name1, PUNICODE_STRING name2, BOOLEAN ignore_case, PVOID UpcaseTable)
{
	if (name1->Length != name2->Length)
		return FALSE;

	if (memcmp(name1->Buffer, name2->Buffer, name1->Length) == 0)
		return TRUE;

	if (!ignore_case)
		return FALSE;

	for (USHORT i = 0; i < name1->Length/sizeof(WCHAR); ++i)
	{
		WCHAR ch1 = name1->Buffer[i];
		WCHAR ch2 = name2->Buffer[i];

		if (os_toupper(ch1) != os_toupper(ch2))
			return FALSE;
	}

	return TRUE;
}

ULONG RtlWalkFrameChain(void** chain, ULONG n1, ULONG n2)
{
	return size_t();
}

NTSTATUS FsRtlNotifyVolumeEvent(PFILE_OBJECT Vol, ULONG Action)
{
	if (Action == FSRTL_VOLUME_MOUNT)
	{
		last_file_object = Vol;
		return STATUS_SUCCESS;
	}
	return STATUS_NOT_IMPLEMENTED;
}

PFILE_OBJECT FsRtlNotifyGetLastVolumeEventsubject()
{
	return last_file_object;
}

NTSTATUS FsRtlNotifyFilterReportChange(PNOTIFY_SYNC NotifySync, PLIST_ENTRY DirNotifyList, PSTRING fn, USHORT name_offset, PSTRING stream, PVOID arg1, ULONG filter_match, ULONG action, PVOID arg2, PVOID arg3)
{
	return STATUS_NOT_IMPLEMENTED;
}

LONG InterlockedIncrement(LONG volatile* val)
{
	return os_interlocked_increment(val);
}

LONGLONG InterlockedIncrement64(LONGLONG volatile* val)
{
	return os_interlocked_increment64(val);
}

LONG InterlockedDecrement(LONG volatile* val)
{
	return os_interlocked_decrement(val);
}

void ExFreeToNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside, PVOID Entry)
{
	ExFreePool(Entry);
}

void ExFreeToPagedLookasideList(PPAGED_LOOKASIDE_LIST Lookaside, PVOID Entry)
{
	ExFreePool(Entry);
}

void ExDeletePagedLookasideList(PPAGED_LOOKASIDE_LIST Lookaside)
{
}

void ExDeleteNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside)
{
}

void FsRtlUninitializeFileLock(PFILE_LOCK FileLock)
{
}

void FsRtlUninitializeOplock(POPLOCK Oplock)
{
}

void RtlFreeUnicodeString(PUNICODE_STRING String)
{
}

BOOLEAN CcUninitializeCacheMap(PFILE_OBJECT FileObject, PVOID arg1, PVOID arg2)
{
	return TRUE;
}

std::mutex vpb_mutex;

void IoAcquireVpbSpinLock(PKIRQL Irql)
{
	vpb_mutex.lock();
}

void IoReleaseVpbSpinLock(KIRQL Irql)
{
	vpb_mutex.unlock();
}

void KeSetTimer(PKTIMER Timer, LARGE_INTEGER Time, PVOID arg1)
{
	Timer->Timer = Time.QuadPart * -1;
}

void IoDetachDevice(PDEVICE_OBJECT DeviceObject)
{
}

void CcSetFileSizes(PFILE_OBJECT FileObject, PCC_FILE_SIZES FileSizes)
{
}

NTSTATUS GetExceptionCode()
{
	return STATUS_NOT_IMPLEMENTED;
}

void KeQuerySystemTime(PLARGE_INTEGER Time)
{
}

void IoRemoveShareAccess(PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess)
{
}

PEPROCESS IoGetRequestorProcess(PIRP Irp)
{
	return PEPROCESS();
}

NTSTATUS FsRtlFastUnlockAll(PFILE_LOCK FileLock, PFILE_OBJECT FileObject, PEPROCESS Requestor, PVOID arg1)
{
	return STATUS_NOT_IMPLEMENTED;
}

void FsRtlNotifyCleanup(PNOTIFY_SYNC NotifySync, PLIST_ENTRY NotifyList, PVOID arg1)
{
}



void CcPurgeCacheSection(PSECTION_OBJECT_POINTERS SectionPointers, PLARGE_INTEGER FileOffset, ULONG Length, ULONG Flags)
{
}

void MmProbeAndLockPages(PMDL Mdl, REQMODE Reqmode, ACCESS_MODE AccessMode)
{
}

void MmUnlockPages(PMDL Mdl)
{
}

PIRP IoBuildDeviceIoControlRequest(ULONG ControlCode, PDEVICE_OBJECT DeviceObject, PVOID InputBuffer, ULONG InputBufferSize, PVOID OutputBuffer, ULONG OutputBufferSize, BOOLEAN Internal, PKEVENT Event, PIO_STATUS_BLOCK IoStatusBlock)
{
	PIRP Irp = new IRP;
	memset(Irp, 0, sizeof(IRP));

	if (Internal)
		Irp->StackLocation.MajorFunction = IRP_MJ_DEVICE_CONTROL;
	else
		Irp->StackLocation.MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;

	Irp->AssociatedIrp.SystemBuffer = OutputBuffer;
	if (InputBuffer != OutputBuffer)
	{
		Irp->MdlAddress = reinterpret_cast<PMDL>(InputBuffer);
	}
	Irp->StackLocation.Parameters.DeviceIoControl.IoControlCode = ControlCode;
	Irp->StackLocation.Parameters.DeviceIoControl.InputBufferLength = InputBufferSize;
	Irp->StackLocation.Parameters.DeviceIoControl.OutputBufferLength = OutputBufferSize;
	Irp->UserEvent = Event;
	*IoStatusBlock = Irp->IoStatus;
	return Irp;
}

void CcInitializeCacheMap(PFILE_OBJECT FileObject, PCC_FILE_SIZES CcFileSizes, BOOLEAN PinAccess, PCACHE_MANAGER_CALLBACKS Callbacks, PVOID Context)
{
}

void CcSetReadAheadGranularity(PFILE_OBJECT FileObject, ULONG Size)
{
}

void KeInitializeSpinLock(PKSPIN_LOCK Spinlock)
{
	//TODO: Use WaitOnAddress
	Spinlock->Lock = new KSPIN_LOCK_IMPL;
}

void InitializeObjectAttributes(POBJECT_ATTRIBUTES ObjectAttributes, PUNICODE_STRING arg1, OBJECT_ATTRIBUTE_TYPE Type, PVOID arg2, PVOID arg3)
{
	if (arg1 != NULL)
	{
		ULONG len = (std::min)(sizeof(ObjectAttributes->path) - 1, arg1->Length / sizeof(WCHAR));
		memcpy(ObjectAttributes->path, arg1->Buffer, len * sizeof(WCHAR));
		ObjectAttributes->path[len] = 0;
	}
}

NTSTATUS PsCreateSystemThread(HANDLE* hThread, ULONG access, POBJECT_ATTRIBUTES attr, HANDLE process, PVOID arg1, PKSTART_ROUTINE Routine, PVOID Context)
{
	std::thread* new_thread = new std::thread(Routine, Context);
	new_thread->detach();
	*hThread = addTypeTag(static_cast<HANDLE>(new_thread), handle_tt_thread);
	return STATUS_SUCCESS;
}

std::atomic<size_t> max_volumes(0);

NTSTATUS IoGetDeviceInterfaces(PGUID arg1, PVOID arg2, ULONG len, WCHAR** list)
{
	std::wstring devlist;
	if (memcmp(arg1, &GUID_DEVINTERFACE_DISK, sizeof(GUID)) == 0)
	{		
		for (size_t i = 0; i < max_volumes; ++i)
		{
			devlist += L"VOL" + std::to_wstring(i);
			devlist += wchar_t{ 0 };
		}
	}
	*list = reinterpret_cast<WCHAR*>(ExAllocate((devlist.size() + 1) * sizeof(WCHAR)));
	memcpy(*list, devlist.data(), devlist.size() * sizeof(wchar_t));
	(*list)[devlist.size()] = 0;
	return STATUS_SUCCESS;
}

NTSTATUS IoRegisterDeviceObjectPointer(PUNICODE_STRING Name, PFILE_OBJECT FileObject, PDEVICE_OBJECT DeviceObject)
{
	std::wstring wname(Name->Buffer, Name->Length/sizeof(WCHAR));

	auto& it = registered_device_objects[wname];
	it.DeviceObject = DeviceObject;
	it.FileObject = FileObject;

	return STATUS_SUCCESS;
}

NTSTATUS IoGetDeviceObjectPointer(PUNICODE_STRING Name, ULONG ObjectPointerType, PFILE_OBJECT* FileObject, PDEVICE_OBJECT* DeviceObject)
{
	std::wstring wname(Name->Buffer, Name->Length / sizeof(WCHAR));

	auto it = registered_device_objects.find(wname);

	if (it != registered_device_objects.end())
	{
		*FileObject = it->second.FileObject;
		*DeviceObject = it->second.DeviceObject;
		return STATUS_SUCCESS;
	}
	else
	{
		return STATUS_NOT_FOUND;
	}
}

void ObReferenceObject(PVOID Obj)
{
}

PDEVICE_OBJECT IoGetLowerDeviceObject(PDEVICE_OBJECT DeviceObject)
{
	return PDEVICE_OBJECT();
}



NTSTATUS IoCreateDevice(PDRIVER_OBJECT DriverObject, ULONG Length, PUNICODE_STRING Name, DEVICE_TYPE Type, ULONG Characteristics, BOOLEAN Exclusive, PDEVICE_OBJECT* DeviceObject)
{
	PDEVICE_OBJECT devobj = new DEVICE_OBJECT;
	memset(devobj, 0, sizeof(devobj));
	devobj->DriverObject = DriverObject;
	devobj->DeviceExtension = new char[Length];
	memset(devobj->DeviceExtension, 0, Length);
	*DeviceObject = devobj;

	if (Type == FILE_DEVICE_DISK)
	{
		register_new_disk(devobj);
	}
	return STATUS_SUCCESS;
}

BOOLEAN IoIsErrorUserInduced(NTSTATUS Status)
{
	return BOOLEAN();
}

void IoSetHardErrorOrVerifyDevice(PIRP Irp, PDEVICE_OBJECT DeviceObject)
{
}

NTSTATUS ZwQueryInformationProcess(HANDLE ProcessHandle, PROCESSINFOCLASS InformationType, PVOID info, ULONG info_length, PULONG ret_len)
{
	return STATUS_NOT_IMPLEMENTED;
}

void ExInitializeFastMutex(PFAST_MUTEX FastMutex)
{
	//TODO: Convert to futex (WaitOnAddress etc...)
	FastMutex->fast_mutex = reinterpret_cast<FAST_MUTEX_OP*>(new std::mutex);
}

void FsRtlNotifyInitializeSync(PNOTIFY_SYNC* NotifySync)
{
}

void ExInitializePagedLookasideList(PPAGED_LOOKASIDE_LIST LookasideList, PVOID arg1, PVOID arg2, ULONG Flags, SIZE_T Size, ULONG Tag, USHORT Depth)
{
	LookasideList->entry_size = Size;
}

void ExInitializeNPagedLookasideList(PNPAGED_LOOKASIDE_LIST LookasideList, PVOID arg1, PVOID arg2, ULONG Flags, SIZE_T Size, ULONG Tag, USHORT Depth)
{
	LookasideList->entry_size = Size;
}

void IoRaiseInformationalHardError(NTSTATUS Status, PUNICODE_STRING Msg, PKTHREAD Thread)
{
}

PFILE_OBJECT IoCreateStreamFileObject(PVOID arg1, PDEVICE_OBJECT DeviceObject)
{
	PFILE_OBJECT ret = new FILE_OBJECT;
	ret->DeviceObject = DeviceObject;
	return ret;
}

NTSTATUS FsRtlProcessFileLock(PFILE_LOCK FileLock, PIRP Irp, PVOID arg1)
{
	return NTSTATUS();
}

void RtlInitUnicodeString(PUNICODE_STRING InitStr, WCHAR* Str)
{
	size_t len = 0;
	if (Str != NULL)
	{
		while (Str[len] != 0)
		{
			++len;
		}
	}

	if (len > 60000)
		abort();

	InitStr->Buffer = new wchar_t[static_cast<size_t>(len) + 1];
	InitStr->Length = len*sizeof(WCHAR);
	InitStr->MaximumLength = len*sizeof(WCHAR);
	memcpy(InitStr->Buffer, Str, len * sizeof(wchar_t));
}

void IoUnregisterFileSystem(PDEVICE_OBJECT DeviceObject)
{
}

void IoUnregisterPlugPlayNotification(PVOID Entry)
{
}

void PoStartNextPowerIrp(PIRP Irp)
{
}

void IoSkipCurrentIrpStackLocation(PIRP Irp)
{

}

NTSTATUS PoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	return NTSTATUS();
}

PVOID ExAllocateFromNPagedLookasideList(PNPAGED_LOOKASIDE_LIST LookasideList)
{
	return ExAllocate(LookasideList->entry_size);
}

PVOID ExAllocateFromPagedLookasideList(PPAGED_LOOKASIDE_LIST LookasideList)
{
	return ExAllocate(LookasideList->entry_size);
}

void KeInitializeTimer(PKTIMER Timer)
{
	Timer->Set = 0;
	Timer->Slot = 0;
	Timer->Timer = 0;
}

void KeCancelTimer(PKTIMER Timer)
{
}

void PsTerminateSystemThread(NTSTATUS Status)
{
}

NTSTATUS ZwCreateFile(HANDLE* pHandle, ULONG PointerType, POBJECT_ATTRIBUTES ObjectAttributes,
	PIO_STATUS_BLOCK IoStatusBlock, PVOID arg1, ULONG Attributes, ULONG ShareMode, ULONG OpenMode, ULONG Flags, PVOID arg2, ULONG arg3)
{
	int mode = 0;

	if (OpenMode == FILE_OPEN_IF)
	{
		mode = MODE_RW_CREATE;
	}
	else
	{
		assert(false);
	}

	IoStatusBlock->Information = 0;

	File* file = new File();

	std::string fpath = ConvertFromWchar(ObjectAttributes->path);

	if (fpath.find("\\??\\") == 0)
		fpath = fpath.substr(4);

	if (mode == MODE_RW_CREATE &&
			file->Open(fpath, MODE_RW))
	{
		IoStatusBlock->Information |= FILE_OPENED;
	}
	else
	{
		if (!file->Open(fpath, mode))
		{
			delete file;
			return STATUS_UNSUCCESSFUL;
		}
	}

	*pHandle = addTypeTag(static_cast<HANDLE>(file), handle_tt_file);

	return STATUS_SUCCESS;
}

NTSTATUS ZwQueryInformationFile(HANDLE Handle, PIO_STATUS_BLOCK IoStatusBlock, PVOID Out, ULONG OutLen, FILE_INFORMATION_CLASS InformationType)
{
	if (InformationType == FileStandardInformation)
	{
		unsigned short tt;
		Handle = removeTypeTag(Handle, tt);

		if (tt != handle_tt_file)
			return STATUS_INVALID_PARAMETER;

		File* file = static_cast<File*>(Handle);
		PFILE_STANDARD_INFORMATION fsi = static_cast<PFILE_STANDARD_INFORMATION>(Out);
		fsi->EndOfFile.QuadPart = file->Size();
		fsi->AllocationSize.QuadPart = 4096;
		fsi->DeletePending = FALSE;
		fsi->Directory = FALSE;
		fsi->NumberOfLinks = 1;
		return STATUS_SUCCESS;
	}

	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS ZwSetInformationFile(HANDLE Handle, PIO_STATUS_BLOCK IoStatusBlock, PVOID In, ULONG InLen, FILE_INFORMATION_CLASS InformationType)
{
	if (InformationType == FilePositionInformation)
	{
		unsigned short tt;
		Handle = removeTypeTag(Handle, tt);

		if (tt != handle_tt_file)
			return STATUS_INVALID_PARAMETER;

		File* file = static_cast<File*>(Handle);
		PFILE_POSITION_INFORMATION fpi = static_cast<PFILE_POSITION_INFORMATION>(Handle);
		if (file->Seek(fpi->CurrentByteOffset.QuadPart))
		{
			return STATUS_SUCCESS;
		}
		else
		{
			return STATUS_UNSUCCESSFUL;
		}
	}
	return NTSTATUS();
}

void RtlTimeToTimeFields(PLARGE_INTEGER Time, PTIME_FIELDS TimeFields)
{
}

NTSTATUS IoCreateSymbolicLink(PUNICODE_STRING Name, PUNICODE_STRING Target)
{
	return NTSTATUS();
}

NTSTATUS IoRegisterDeviceInterface(PDEVICE_OBJECT DeviceObject, PGUID Guid, PVOID arg1, PUNICODE_STRING Name)
{
	return NTSTATUS();
}

PDEVICE_OBJECT IoAttachDeviceToDeviceStack(PDEVICE_OBJECT DeviceObject, PDEVICE_OBJECT AttachDeviceObject)
{
	PDEVICE_OBJECT ret = AttachDeviceObject->AttachedDevice;
	AttachDeviceObject->AttachedDevice = DeviceObject;
	return ret;
}

NTSTATUS IoSetDeviceInterfaceState(PUNICODE_STRING Name, BOOLEAN set)
{
	return NTSTATUS();
}

PVOID MmGetSystemRoutineAddress(PUNICODE_STRING Name)
{
	return PVOID();
}

NTSTATUS ZwCreateKey(HANDLE* hKey, ULONG Action, POBJECT_ATTRIBUTES ObjectAttributes, ULONG arg1, PVOID arg2, ULONG Flags, PULONG Disposition)
{
	*hKey = NULL;
	return NTSTATUS();
}

NTSTATUS IoReportDetectedDevice(PDRIVER_OBJECT DriverObject, REPORT_INTERFACE_TYPE InterfaceType, ULONG arg1, ULONG arg2, PVOID arg3, PVOID arg4, ULONG arg5, PDEVICE_OBJECT* DeviceObject)
{
	if (DeviceObject != NULL
		&& *DeviceObject == NULL)
	{
		*DeviceObject = new DEVICE_OBJECT;
	}

	return STATUS_SUCCESS;
}

void IoInvalidateDeviceRelations(PDEVICE_OBJECT DeviceObject, DEVICE_RELATIONS_TYPE DeviceRelation)
{
}

NTSTATUS IoRegisterPlugPlayNotification(PLUG_N_PLAY_EVENT_CATEGORIES EventCategory, ULONG Flags, PVOID arg1, PDRIVER_OBJECT DriverObject1, PNPNOTIFICATION_FUN NotificationFun, PVOID Context, PVOID arg2)
{
	return NTSTATUS();
}

void IoRegisterFileSystem(PDEVICE_OBJECT DeviceObject)
{
}

void IoRegisterBootDriverReinitialization(PDRIVER_OBJECT DriverObject, BOOTDRIVERINIT_FUN InitFun, PVOID Context)
{
}

LUID RtlConvertLongToLuid(LONG Long)
{
	LUID ret;
	ret.LowPart = Long;
	ret.HighPart = 0;
	return ret;
}

BOOLEAN SeSinglePrivilegeCheck(LUID Lid, KPROCESSOR_MODE processor_mode)
{
	return TRUE;
}

ULONG RtlRealBitmapSize(ULONG Size)
{
	ULONG bitmap_size = (Size + 7) / 8;
	bitmap_size = (bitmap_size + sizeof(ULONG) - 1) / sizeof(ULONG);
	return bitmap_size;
}

void RtlInitializeBitMap(PRTL_BITMAP Bitmap, PULONG Buffer, ULONG Size)
{
	Bitmap->SizeOfBitMap = Size;
	Bitmap->Buffer = new ULONG[RtlRealBitmapSize(Size)];
}

void RtlSetAllBits(PRTL_BITMAP Bitmap)
{
	memset(Bitmap->Buffer, 0xFF, RtlRealBitmapSize(Bitmap->SizeOfBitMap) * sizeof(ULONG));
}

void RtlSetBitInt(PRTL_BITMAP Bitmap, ULONG Idx, BOOLEAN v)
{
	const ULONG bits_per = 8 * sizeof(ULONG);

	ULONG bitmap_long = Idx / bits_per;
	ULONG bitmap_bit = Idx % bits_per;

	ULONG b = Bitmap->Buffer[bitmap_long];

	if (v)
		b = b | (1 << (bits_per - 1 - bitmap_bit));
	else
		b = b & (~(1 << (bits_per - 1 - bitmap_bit)));

	Bitmap->Buffer[bitmap_long] = b;
}

void RtlClearBits(PRTL_BITMAP Bitmap, ULONG Start, ULONG Len)
{
	for (ULONG i = 0; i < Len; ++i)
	{
		RtlSetBitInt(Bitmap, Start + i, FALSE);
	}
}

void RtlSetBit(PRTL_BITMAP Bitmap, ULONG Off)
{
	RtlSetBitInt(Bitmap, Off, TRUE);
}

void RtlClearAllBits(PRTL_BITMAP Bitmap)
{
	memset(Bitmap->Buffer, 0, RtlRealBitmapSize(Bitmap->SizeOfBitMap) * sizeof(ULONG));
}

void RtlSetBits(PRTL_BITMAP Bitmap, ULONG Start, ULONG Len)
{
	for (ULONG i = 0; i < Len; ++i)
	{
		RtlSetBitInt(Bitmap, Start + i, TRUE);
	}
}

BOOLEAN RtlGetBit(PRTL_BITMAP Bitmap, ULONG Idx)
{
	const ULONG bits_per = 8 * sizeof(ULONG);

	ULONG bitmap_long = Idx / bits_per;
	ULONG bitmap_bit = Idx % bits_per;

	ULONG b = Bitmap->Buffer[bitmap_long];

	return ((b & (1 << (bits_per - 1 - bitmap_bit))) > 0) ? TRUE : FALSE;
}

BOOLEAN RtlAreBitsClear(PRTL_BITMAP Bitmap, ULONG Start, ULONG Len)
{
	for (ULONG i = 0; i < Len; ++i)
	{
		if (RtlGetBit(Bitmap, Start + i))
			return FALSE;
	}
	return TRUE;
}

BOOLEAN RtlCheckBit(PRTL_BITMAP Bitmap, ULONG Off)
{
	return RtlGetBit(Bitmap, Off);
}

ULONG RtlFindNextForwardRunClear(PRTL_BITMAP Bitmap, ULONG Off, PULONG Start)
{
	ULONG i;
	ULONG len = 0;
	for (i = Off; i < Bitmap->SizeOfBitMap; ++i)
	{
		if (!RtlGetBit(Bitmap, i))
		{
			*Start = i;
			len = 1;
			++i;
			break;
		}
	}

	for (; i < Bitmap->SizeOfBitMap; ++i)
	{
		if (RtlGetBit(Bitmap, i))
		{
			break;
		}
		else
		{
			++len;
		}
	}

	return len;
}

ULONG RtlFindFirstRunClear(PRTL_BITMAP Bitmap, PULONG Start)
{
	return RtlFindNextForwardRunClear(Bitmap, 0, Start);
}

NTSTATUS ZwOpenSymbolicLinkObject(HANDLE hObj, ULONG OpenMode, POBJECT_ATTRIBUTES Attrs)
{
	return NTSTATUS();
}

NTSTATUS ZwQuerySymbolicLinkObject(HANDLE hObj, PUNICODE_STRING Target, PULONG retlen)
{
	return NTSTATUS();
}

void NtClose(HANDLE hObj)
{

}

NTSTATUS ZwQueryValueKey(HANDLE hObj, PUNICODE_STRING Name, QUERY_VALUE_TYPE Type, PKEY_VALUE_FULL_INFORMATION KvInf, ULONG KvInfLen, PULONG KvInfLenOut)
{
	std::wstring wname;
	wname.resize(Name->Length/sizeof(WCHAR));
	memcpy(&wname[0], Name->Buffer, Name->Length);
	if (wname == L"DebugLogLevel")
	{
		if (KvInf == NULL || KvInfLen < sizeof(KEY_VALUE_FULL_INFORMATION)+ sizeof(DWORD))
		{
			if (KvInfLenOut != NULL)
				*KvInfLenOut = sizeof(KEY_VALUE_FULL_INFORMATION) + sizeof(DWORD);

			return STATUS_BUFFER_TOO_SMALL;
		}

		if (KvInfLenOut != NULL)
			*KvInfLenOut = sizeof(KEY_VALUE_FULL_INFORMATION) + sizeof(DWORD);

		KvInf->DataLength = sizeof(DWORD);
		KvInf->DataOffset = offsetof(KEY_VALUE_FULL_INFORMATION, Name);
		KvInf->NameLength = 0;
		KvInf->Type = REG_DWORD;

		DWORD out = 10;
		memcpy((char*)KvInf + KvInf->DataOffset, &out, sizeof(out));

		return STATUS_SUCCESS;
	}

	return NTSTATUS();
}

PKTHREAD KeGetCurrentThread()
{
	return PKTHREAD();
}

void KeAcquireSpinLock(PKSPIN_LOCK Spinlock, PKIRQL Irql)
{
	//TODO: Use WaitOnAddress
	Spinlock->Lock->m.lock();
}

void KeReleaseSpinLock(PKSPIN_LOCK Spinlock, KIRQL Irql)
{
	Spinlock->Lock->m.unlock();
}

void KeSetSystemAffinityThread(KAFFINITY Affinity)
{
}

void FsRtlSetupAdvancedHeader(PFSRTL_ADVANCED_FCB_HEADER Header, PFAST_MUTEX FastMutex)
{
}

void FsRtlInitializeFileLock(PFILE_LOCK FileLock, PVOID arg1, PVOID arg2)
{
}

void FsRtlInitializeOplock(POPLOCK Oplock)
{
}

NTSTATUS IoCheckEaBufferValidity(PFILE_FULL_EA_INFORMATION EaInformation, ULONG Length, PULONG Offset)
{
	return NTSTATUS();
}

BOOLEAN RtlValidRelativeSecurityDescriptor(PVOID SecurityDescriptor, ULONG Length, PVOID arg1)
{
	return BOOLEAN();
}

void SeLockSubjectContext(PSECURITY_SUBJECT_CONTEXT SecSubjContext)
{
}

BOOLEAN SeAccessCheck(PSECURITY_DESCRIPTOR Sd, PSECURITY_SUBJECT_CONTEXT SecSubjContext, BOOLEAN b, ULONG FileOption, ULONG arg1, PVOID arg2, PGENERIC_MAPPING GenMap, REQMODE Reqmode, ACCESS_MASK* Access, NTSTATUS* status)
{
	*Access = FileOption;
	*status = STATUS_SUCCESS;
	return TRUE;
}

PGENERIC_MAPPING IoGetFileObjectGenericMapping()
{
	return PGENERIC_MAPPING();
}

void IoSetShareAccess(ACCESS_MASK Access, ULONG DesiredAccess, PFILE_OBJECT FileObj, PSHARE_ACCESS ShareAccess)
{
}

BOOLEAN FsRtlDoesNameContainWildCards(PUNICODE_STRING Str)
{
	for (USHORT i = 0; i < Str->Length/sizeof(WCHAR); ++i)
	{
		if (Str->Buffer[i] == '*')
			return TRUE;
	}
	return FALSE;
}

BOOLEAN FsRtlIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PVOID arg1)
{
	//TODO: Implement
	return FALSE;
}

void FsRtlNotifyFilterChangeDirectory(PNOTIFY_SYNC NotifySync, PLIST_ENTRY DirList, PVOID FsContext, PSTRING Filename, ULONG Flags, BOOLEAN b1, ULONG CompletionFilter, PIRP Irp, PVOID arg1, PVOID arg2, PVOID arg3)
{
}

void FsRtlNotifyFullChangeDirectory(PNOTIFY_SYNC NotifySync, PLIST_ENTRY DirList, PVOID FsContext, PVOID arg1, BOOLEAN arg2, BOOLEAN arg3, ULONG arg4, PVOID arg5, PVOID arg6, PVOID arg7)
{
}

BOOLEAN FsRtlCopyRead(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, ULONG LockKey, PVOID Buf, PIO_STATUS_BLOCK Status, PDEVICE_OBJECT DeviceObject)
{
	return BOOLEAN();
}

BOOLEAN FsRtlMdlReadDev(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, ULONG LockKey, PMDL* Chain, PIO_STATUS_BLOCK Status, PDEVICE_OBJECT DeviceObject)
{
	return BOOLEAN();
}

BOOLEAN FsRtlMdlReadCompleteDev(PFILE_OBJECT FileObject, PMDL Chain, PDEVICE_OBJECT DeviceObject)
{
	return BOOLEAN();
}

BOOLEAN FsRtlPrepareMdlWriteDev(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, ULONG LockKey, PMDL* Chain, PIO_STATUS_BLOCK Status, PDEVICE_OBJECT DeviceObject)
{
	return BOOLEAN();
}

BOOLEAN FsRtlMdlWriteCompleteDev(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, PMDL Chain, PDEVICE_OBJECT DeviceObject)
{
	return BOOLEAN();
}

BOOLEAN FsRtlFastCheckLockForRead(PFILE_LOCK FileLock, PLARGE_INTEGER Offset, PLARGE_INTEGER Len, ULONG Key, PFILE_OBJECT FileObj, PVOID ProcId)
{
	return BOOLEAN();
}

BOOLEAN FsRtlFastCheckLockForWrite(PFILE_LOCK FileLock, PLARGE_INTEGER Offset, PLARGE_INTEGER Len, ULONG Key, PFILE_OBJECT FileObj, PVOID ProcId)
{
	return BOOLEAN();
}

BOOLEAN FsRtlCopyWrite(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, ULONG LockKey, PVOID Buf, PIO_STATUS_BLOCK Status, PDEVICE_OBJECT DeviceObject)
{
	return BOOLEAN();
}

BOOLEAN FsRtlFastLock(PFILE_LOCK FileLock, PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, PLARGE_INTEGER Len, PEPROCESS ProcID, ULONG Key, BOOLEAN FailImmediately, BOOLEAN ExclusiveLock, PIO_STATUS_BLOCK Status, PVOID arg1, BOOLEAN arg2)
{
	return BOOLEAN();
}

NTSTATUS FsRtlFastUnlockSingle(PFILE_LOCK FileLock, PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, PLARGE_INTEGER Len, PEPROCESS ProcID, ULONG Key, PVOID arg1, BOOLEAN arg2)
{
	return NTSTATUS();
}

NTSTATUS FsRtlFastUnlockAllByKey(PFILE_LOCK FileLock, PFILE_OBJECT FileObject, PEPROCESS ProcID, ULONG Key, PVOID arg1)
{
	return NTSTATUS();
}

ULONG RtlLengthSecurityDescriptor(PSECURITY_DESCRIPTOR SecurityDescriptor)
{
	return 0;
}

void SeCaptureSubjectContext(PSECURITY_SUBJECT_CONTEXT SecurityContext)
{
}

void SeReleaseSubjectContext(PSECURITY_SUBJECT_CONTEXT SecurityContext)
{
}

NTSTATUS SeAssignSecurity(PSECURITY_DESCRIPTOR SecurityDescriptor, PVOID arg1, PVOID* SdLink, BOOLEAN arg2, PSECURITY_SUBJECT_CONTEXT SecurityContext, PGENERIC_MAPPING Mapping, POOL_TYPE PoolType)
{
	*SdLink = reinterpret_cast<void*>(0x1);
	return STATUS_SUCCESS;
}

NTSTATUS RtlGetOwnerSecurityDescriptor(PSECURITY_DESCRIPTOR SecurityDescriptor, PSID* Owner, BOOLEAN* defaulted)
{
	static SID lsid = {};
	*Owner = &lsid;
	return STATUS_SUCCESS;
}

LARGE_INTEGER KeQueryPerformanceCounter(PLARGE_INTEGER arg1)
{
	LARGE_INTEGER ret;
	if (arg1 != NULL)
		ret.QuadPart = os_perf_counter(&arg1->QuadPart);
	else
		ret.QuadPart = os_perf_counter(NULL);

	return ret;
}

ULONG RtlRandomEx(PULONG Prev)
{
	ULONG ret = os_rand_next(*Prev);
	*Prev = ret;
	return ret;
}

BOOLEAN IoIs32bitProcess(PIRP Irp)
{
	return BOOLEAN();
}

void SeUnlockSubjectContext(PSECURITY_SUBJECT_CONTEXT SecSubjContext)
{
}

BOOLEAN MmFlushImageSection(PSECTION_OBJECT_POINTERS SectionPointers, FLUSH_MODE FlushMode)
{
	return TRUE;
}

BOOLEAN MmCanFileBeTruncated(PSECTION_OBJECT_POINTERS SectionPointers, PLARGE_INTEGER Size)
{
	return BOOLEAN();
}

NTSTATUS IoCheckShareAccess(ACCESS_MASK AccessMask, ULONG Granted, PFILE_OBJECT FileObject, PSHARE_ACCESS ShareAccess, BOOLEAN b1)
{
	return NTSTATUS();
}

void IoUpdateShareAccess(PFILE_OBJECT, PSHARE_ACCESS ShareAccess)
{
}

PDEVICE_OBJECT IoGetDeviceToVerify(PETHREAD Thread)
{
	return PDEVICE_OBJECT();
}

void IoSetDeviceToVerify(PETHREAD Thread, PDEVICE_OBJECT DevObj)
{
}

NTSTATUS IoVerifyVolume(PDEVICE_OBJECT DevObj, BOOLEAN verify)
{
	return NTSTATUS();
}

BOOLEAN SePrivilegeCheck(PPRIVILEGE_SET PrivilegeSet, PSECURITY_SUBJECT_CONTEXT SecSubjContext, KPROCESSOR_MODE ProcessorMode)
{
	return BOOLEAN();
}

HANDLE ObRegisterHandle(PVOID Object)
{
	PVOID* reg = new PVOID;
	*reg = Object;

	return addTypeTag(reg, handle_tt_registered);
}

NTSTATUS ObDeregisterHandle(HANDLE Handle)
{
	unsigned short tt;
	PVOID* reg = reinterpret_cast<PVOID*>(removeTypeTag(Handle, tt));
	if (tt != handle_tt_registered)
	{
		return STATUS_INVALID_PARAMETER;
	}

	delete reg;
	return STATUS_SUCCESS;
}

NTSTATUS ObReferenceObjectByHandle(HANDLE Handle, ACCESS_MASK Access,
	POBJECT_TYPE ObjectType, KPROCESSOR_MODE ProcMode, PVOID* Object, PVOID arg1)
{
	unsigned short tt;
	PVOID* reg = reinterpret_cast<PVOID*>(removeTypeTag(Handle, tt));
	if (tt != handle_tt_registered)
	{
		return STATUS_INVALID_PARAMETER;
	}

	*Object = *reg;

	return STATUS_SUCCESS;
}

HANDLE LongToHandle(ULONG tLong)
{
	return HANDLE();
}

void CcWaitForCurrentLazyWriterActivity()
{
}

NTSTATUS SeAssignSecurityEx(PSECURITY_DESCRIPTOR SecurityDescriptor, PVOID arg1, PVOID* Sd, PVOID arg2, BOOLEAN Directory, ULONG Flags, PSECURITY_SUBJECT_CONTEXT SecSubjContext, PGENERIC_MAPPING Mapping, POOL_TYPE PoolType)
{
	return NTSTATUS();
}

NTSTATUS FsRtlOplockFsctrl(POPLOCK Oplock, PIRP Irp, ULONG Count)
{
	return NTSTATUS();
}

BOOLEAN IsReparseTagMicrosoft(ULONG ReparseTag)
{
	return BOOLEAN();
}

void IoAdjustPagingPathCount(LONG* Count, BOOL b1)
{
}

PIRP IoMakeAssociatedIrp(PIRP Irp, char StackSize)
{
	PIRP ret = IoAllocateIrp(StackSize, FALSE);
	Irp->AssociatedIRP = ret;
	return ret;
}

void CcMdlRead(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, PMDL* Address, PIO_STATUS_BLOCK Status)
{
}

void IoMarkIrpPending(PIRP Irp)
{

}

BOOLEAN CcCopyRead(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, PUCHAR Data, PIO_STATUS_BLOCK Status)
{
	return FALSE;
}

BOOLEAN IoIsSystemThread(PETHREAD Thread)
{
	return BOOLEAN();
}

PEPROCESS PsGetThreadProcess(PETHREAD Thread)
{
	return PEPROCESS();
}

void CcMdlReadComplete(PFILE_OBJECT FileObject, PMDL Mdl)
{
}

BOOLEAN IoIsOperationSynchronous(PIRP Irp)
{
	return TRUE;
}

BOOLEAN FsRtlCheckLockForReadAccess(PFILE_LOCK Lock, PIRP Irp)
{
	return TRUE;
}

NTSTATUS ZwEnumerateValueKey(HANDLE h, ULONG index, REG_ENUM_TYPE EnumyType, PVOID Out, ULONG OutLen, PULONG retlen)
{
	return STATUS_NO_MORE_ENTRIES;
}

NTSTATUS ZwOpenKey(PHANDLE h, ULONG Access, POBJECT_ATTRIBUTES Attr)
{
	*h = NULL;
	return NTSTATUS();
}

NTSTATUS ZwSetValueKey(HANDLE h, PUNICODE_STRING Path, ULONG Idx, ULONG Type, PVOID Data, ULONG DataLen)
{
	return NTSTATUS();
}

NTSTATUS ZwDeleteKey(HANDLE h)
{
	return NTSTATUS();
}

NTSTATUS ZwEnumerateKey(HANDLE h, ULONG Idx, REG_ENUM_TYPE EnumType, PVOID Out, ULONG OutLen, PULONG retlen)
{
	return STATUS_NO_MORE_ENTRIES;
}

NTSTATUS ZwDeleteValueKey(HANDLE h, PUNICODE_STRING Path)
{
	return NTSTATUS();
}

NTSTATUS ZwNotifyChangeKey(HANDLE h, PVOID arg1, PVOID arg2, PVOID arg3, PIO_STATUS_BLOCK IoSb, ULONG Action, BOOLEAN arg4, PVOID arg5, ULONG arg6, BOOLEAN arg7)
{
	return NTSTATUS();
}

void ExInitializeWorkItem(PWORK_QUEUE_ITEM Wqi, WQFUN Fun, HANDLE h)
{
}

NTSTATUS SeQueryInformationToken(PACCESS_TOKEN Token, QUERY_INFORMATION_TOKEN_TYPE TokenType, PVOID Out)
{
	return NTSTATUS();
}

ULONG RtlLengthSid(PSID Sid)
{
	return 0;
}

BOOLEAN RtlEqualSid(PSID SidA, PSID SidB)
{
	return BOOLEAN();
}

NTSTATUS RtlCreateSecurityDescriptor(PSECURITY_DESCRIPTOR Sd, ULONG SdRevision)
{
	return NTSTATUS();
}

NTSTATUS RtlSetOwnerSecurityDescriptor(PSECURITY_DESCRIPTOR Sd, PSID Sid, BOOLEAN arg1)
{
	return NTSTATUS();
}

NTSTATUS RtlSetGroupSecurityDescriptor(PSECURITY_DESCRIPTOR Sd, PSID Sid, BOOLEAN arg1)
{
	return NTSTATUS();
}

NTSTATUS RtlSetDaclSecurityDescriptor(PSECURITY_DESCRIPTOR Sd, BOOLEAN ar1, PACL Acl, BOOLEAN arg2)
{
	return NTSTATUS();
}

NTSTATUS RtlAbsoluteToSelfRelativeSD(PSECURITY_DESCRIPTOR Sd, PVOID arg1, PULONG len)
{
	return NTSTATUS();
}

NTSTATUS RtlSelfRelativeToAbsoluteSD(PSECURITY_DESCRIPTOR Sd, PVOID arg1, PULONG Abslen, PVOID arg2, PULONG DaclLen, PVOID arg3, PULONG SaclLen, PVOID arg4, PULONG OwnerLen, PVOID arg5, PULONG GroupLen)
{
	return NTSTATUS();
}

NTSTATUS SeQuerySecurityDescriptorInfo(PSECURITY_INFORMATION Flags, PSECURITY_DESCRIPTOR Sd, PULONG BufLen, PVOID* SdOut)
{
	return NTSTATUS();
}

NTSTATUS SeSetSecurityDescriptorInfo(PVOID arg1, PSECURITY_INFORMATION Flags, PSECURITY_DESCRIPTOR Sd, PVOID* SdOut, POOL_TYPE PoolType, PGENERIC_MAPPING Mapping)
{
	return NTSTATUS();
}

void ExAcquireFastMutex(PFAST_MUTEX Mutex)
{
	std::mutex* m = reinterpret_cast<std::mutex*>(Mutex->fast_mutex);
	m->lock();
}

void ExReleaseFastMutex(PFAST_MUTEX Mutex)
{
	std::mutex* m = reinterpret_cast<std::mutex*>(Mutex->fast_mutex);
	m->unlock();
}

PVOID MmGetMdlVirtualAddress(PMDL Mdl)
{
	return PVOID();
}

void IoBuildPartialMdl(PMDL MdlAdress, PMDL Mdl, PVOID Ptr, ULONG Len)
{
}

void IoCancelIrp(PIRP Irp)
{
}

BOOLEAN CcCanIWrite(PFILE_OBJECT FileObject, ULONG Length, BOOLEAN Wait, BOOLEAN Deferred)
{
	return BOOLEAN();
}

void CcPrepareMdlWrite(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, PMDL* Mdl, PIO_STATUS_BLOCK Status)
{
}

BOOLEAN CcCopyWrite(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, PVOID Buffer)
{
	return BOOLEAN();
}

BOOLEAN FsRtlCheckLockForWriteAccess(PFILE_LOCK FileLock, PIRP Irp)
{
	return TRUE;
}

void CcMdlWriteComplete(PFILE_OBJECT FileObject, PLARGE_INTEGER Offset, PMDL Mdl)
{
}

BOOLEAN WdmlibRtlIsNtDdiVersionAvailable(ULONG Version)
{
	return BOOLEAN();
}

HANDLE Handle32ToHandle(void* POINTER_32 ptr)
{
	return HANDLE();
}

#ifndef _WIN32
int MultiByteToWideChar(UINT CodePage, DWORD Flags, char* ByteStr, int ByteStrSize, wchar_t* OutStr, int OutStrSize)
{
	return 0;
}

PVOID GetModuleHandle(PVOID p1) { return nullptr; }

BOOL LoadStringW(PVOID module, int resid, WCHAR* str, size_t str_size)
{
	return FALSE;
}

DWORD GetLastError(void)
{
	return errno;
}

HMODULE LoadLibraryW(WCHAR* str)
{
	return nullptr;
}

PVOID GetProcAddress(HMODULE, char* name)
{
	return nullptr;
}

#endif