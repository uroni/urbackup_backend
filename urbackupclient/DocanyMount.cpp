/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "DocanyMount.h"
#include "../Interface/BackupFileSystem.h"
#include "../Interface/Server.h"
#include <dokan/dokan.h>
#include <sddl.h>
#include "../stringtools.h"

static NTSTATUS DOKAN_CALLBACK
DokanCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
    ACCESS_MASK DesiredAccess, ULONG FileAttributes,
    ULONG ShareAccess, ULONG CreateDisposition,
    ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo)
{
    IBackupFileSystem* fs = reinterpret_cast<IBackupFileSystem*>(DokanFileInfo->DokanOptions->GlobalContext);

    size_t offs = 0;
    if (*FileName == '\\')
        offs = 1;

    IFsFile* file = fs->openFile(Server->ConvertFromWchar(FileName + offs), MODE_READ);

    if (file == nullptr)
    {
        return STATUS_ACCESS_DENIED;
    }

    DokanFileInfo->Context = reinterpret_cast<ULONG64>(file);

    return STATUS_SUCCESS;
}

static void DOKAN_CALLBACK
DokanCloseFile(LPCWSTR FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
}

static void DOKAN_CALLBACK
DokanCleanup(LPCWSTR FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    delete reinterpret_cast<IFsFile*>(DokanFileInfo->Context);
    DokanFileInfo->Context = 0;
}

static NTSTATUS DOKAN_CALLBACK
DokanReadFile(LPCWSTR FileName, LPVOID Buffer,
    DWORD BufferLength,
    LPDWORD ReadLength,
    LONGLONG Offset,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    IFsFile* file = reinterpret_cast<IFsFile*>(DokanFileInfo->Context);

    if (file == nullptr)
    {
        IBackupFileSystem* fs = reinterpret_cast<IBackupFileSystem*>(DokanFileInfo->DokanOptions->GlobalContext);

        size_t offs = 0;
        if (*FileName == '\\')
            offs = 1;

        file = fs->openFile(Server->ConvertFromWchar(FileName + offs), MODE_READ);

        if (file == nullptr)
        {
            return STATUS_ACCESS_DENIED;
        }
    }

    bool has_read_error = false;
    *ReadLength = file->Read(Offset, reinterpret_cast<char*>(Buffer), BufferLength, &has_read_error);

    if (DokanFileInfo->Context == 0)
    {
        delete file;
    }

    if (has_read_error)
    {
        if (errno == 23)
            return STATUS_CRC_ERROR;

        return DokanNtStatusFromWin32(errno);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK DokanWriteFile(LPCWSTR FileName, LPCVOID Buffer,
    DWORD NumberOfBytesToWrite,
    LPDWORD NumberOfBytesWritten,
    LONGLONG Offset,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK
DokanFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK DokanGetFileInformation(
    LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    memset(HandleFileInformation, 0, sizeof(BY_HANDLE_FILE_INFORMATION));

    IFsFile* file = reinterpret_cast<IFsFile*>(DokanFileInfo->Context);

    LARGE_INTEGER fsize;
    fsize.QuadPart = file->Size();

    HandleFileInformation->nFileSizeLow = fsize.LowPart;
    HandleFileInformation->nFileSizeHigh = fsize.HighPart;

    IBackupFileSystem* fs = reinterpret_cast<IBackupFileSystem*>(DokanFileInfo->DokanOptions->GlobalContext);

    size_t offs = 0;
    if (*FileName == '\\')
        offs = 1;

    int ftype = fs->getFileType(Server->ConvertFromWchar(FileName + offs));

    if (ftype & EFileType_Directory)
        HandleFileInformation->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
DokanFindFiles(LPCWSTR FileName,
    PFillFindData FillFindData,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    IBackupFileSystem* fs = reinterpret_cast<IBackupFileSystem*>(DokanFileInfo->DokanOptions->GlobalContext);

    size_t offs = 0;
    if (*FileName == '\\')
        offs = 1;

    std::string ldir = Server->ConvertFromWchar(FileName + offs);

    int top_path = 0;
    for (char ch : ldir)
        if (ch == '\\')
            ++top_path;

    std::vector<SFile> files = fs->listFiles(ldir);
    for (SFile& file : files)
    {
        if (!ldir.empty() && top_path == 0 &&
            (file.name == ".hashes" ||
                next(file.name, 0, ".symlink_")))
            continue;

        if (ldir.empty() &&
            file.name.find(".new") != std::string::npos)
            continue;

        WIN32_FIND_DATAW find_data = {};
        std::wstring wfilename = Server->ConvertToWchar(file.name);
        memcpy(find_data.cFileName, wfilename.c_str(), (std::min)(259ULL, wfilename.size()*sizeof(wchar_t)));
        LARGE_INTEGER fsize;
        fsize.QuadPart = file.size;
        find_data.nFileSizeLow = fsize.LowPart;
        find_data.nFileSizeHigh = fsize.HighPart;
        LARGE_INTEGER created;
        created.QuadPart = file.created;
        find_data.ftCreationTime.dwLowDateTime = created.LowPart;
        find_data.ftCreationTime.dwHighDateTime = created.HighPart;
        LARGE_INTEGER last_modified;
        last_modified.QuadPart = file.last_modified;
        find_data.ftLastWriteTime.dwLowDateTime = last_modified.LowPart;
        find_data.ftLastWriteTime.dwHighDateTime = last_modified.HighPart;
        LARGE_INTEGER accessed;
        accessed.QuadPart = file.accessed;
        find_data.ftLastAccessTime.dwLowDateTime = accessed.LowPart;
        find_data.ftLastAccessTime.dwHighDateTime = accessed.HighPart;
        find_data.dwFileAttributes = file.isdir ? FILE_ATTRIBUTE_DIRECTORY : 0;
        FillFindData(&find_data, DokanFileInfo);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
DokanDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK
DokanDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK
DokanMoveFile(LPCWSTR FileName,
    LPCWSTR NewFileName, BOOL ReplaceIfExisting,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK DokanLockFile(LPCWSTR FileName,
    LONGLONG ByteOffset,
    LONGLONG Length,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK DokanSetEndOfFile(
    LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK DokanSetAllocationSize(
    LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK DokanSetFileAttributes(
    LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK
DokanSetFileTime(LPCWSTR FileName, CONST FILETIME* CreationTime,
    CONST FILETIME* LastAccessTime, CONST FILETIME* LastWriteTime,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK
DokanUnlockFile(LPCWSTR FileName, LONGLONG ByteOffset, LONGLONG Length,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK DokanGetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
    PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo)
{
    const TCHAR* szSD = TEXT("D:")       // Discretionary ACL
        TEXT("(A;OICI;GA;;;WD)");    // Allow full control 
                                     // to everyone

    BOOL b = ConvertStringSecurityDescriptorToSecurityDescriptor(
        szSD,
        SDDL_REVISION_1,
        &SecurityDescriptor,
        NULL);

    if (!b)
    {
        return DokanNtStatusFromWin32(GetLastError());
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK DokanSetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG SecurityDescriptorLength,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK DokanGetVolumeInformation(
    LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
    LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
    LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    wcscpy_s(VolumeNameBuffer, VolumeNameSize, L"URBACKUP");

    if (VolumeSerialNumber)
        *VolumeSerialNumber = 0x19831116;
    if (MaximumComponentLength)
        *MaximumComponentLength = 255;

    if (FileSystemFlags)
    {
        *FileSystemFlags = FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK |
            FILE_PERSISTENT_ACLS | FILE_NAMED_STREAMS | FILE_CASE_SENSITIVE_SEARCH |
            FILE_CASE_PRESERVED_NAMES;
    }

    wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, L"NTFS");

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK DokanGetDiskFreeSpace(
    PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes,
    PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo)
{
    *FreeBytesAvailable = 10LL * 1024 * 1024 * 1024 * 1024;
    *TotalNumberOfFreeBytes = *FreeBytesAvailable;
    *TotalNumberOfBytes = *FreeBytesAvailable * 2;

    return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK
DokanFindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK DokanMounted(PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK DokanUnmounted(PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_SUCCESS;
}

bool dokany_mount(IBackupFileSystem* fs, const std::string& mount_path)
{
    DOKAN_OPTIONS dokanOptions = {};

    dokanOptions.Version = DOKAN_VERSION;
    dokanOptions.ThreadCount = 0;

    std::wstring mount_path_w = Server->ConvertToWchar(mount_path);
    dokanOptions.MountPoint = mount_path_w.data();
    //dokanOptions.Options |= DOKAN_OPTION_NETWORK;
    dokanOptions.Options |= DOKAN_OPTION_WRITE_PROTECT;
    dokanOptions.Options |= DOKAN_OPTION_CASE_SENSITIVE;
    dokanOptions.Options |= DOKAN_OPTION_DEBUG;
    dokanOptions.Options |= DOKAN_OPTION_STDERR;
    dokanOptions.GlobalContext = reinterpret_cast<ULONG64>(fs);

    DOKAN_OPERATIONS dokanOperations = {};

    dokanOperations.ZwCreateFile = DokanCreateFile;
    dokanOperations.Cleanup = DokanCleanup;
    dokanOperations.CloseFile = DokanCloseFile;
    dokanOperations.ReadFile = DokanReadFile;
    dokanOperations.WriteFile = DokanWriteFile;
    dokanOperations.FlushFileBuffers = DokanFlushFileBuffers;
    dokanOperations.GetFileInformation = DokanGetFileInformation;
    dokanOperations.FindFiles = DokanFindFiles;
    dokanOperations.FindFilesWithPattern = NULL;
    dokanOperations.SetFileAttributes = DokanSetFileAttributes;
    dokanOperations.SetFileTime = DokanSetFileTime;
    dokanOperations.DeleteFile = DokanDeleteFile;
    dokanOperations.DeleteDirectory = DokanDeleteDirectory;
    dokanOperations.MoveFile = DokanMoveFile;
    dokanOperations.SetEndOfFile = DokanSetEndOfFile;
    dokanOperations.SetAllocationSize = DokanSetAllocationSize;
    dokanOperations.LockFile = DokanLockFile;
    dokanOperations.UnlockFile = DokanUnlockFile;
    dokanOperations.GetFileSecurity = DokanGetFileSecurity;
    dokanOperations.SetFileSecurity = DokanSetFileSecurity;
    dokanOperations.GetDiskFreeSpace = DokanGetDiskFreeSpace;
    dokanOperations.GetVolumeInformation = DokanGetVolumeInformation;
    dokanOperations.Unmounted = DokanUnmounted;
    dokanOperations.FindStreams = DokanFindStreams;
    dokanOperations.Mounted = DokanMounted;

    int status = DokanMain(&dokanOptions, &dokanOperations);

    if (status != DOKAN_SUCCESS)
        return false;

	return true;
}
