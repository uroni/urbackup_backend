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

#include <iostream>
#include <vector>
#include <assert.h>
#include <atomic>
#include "oslib/os.h"
#include "../../utf8/utf8.h"
#include "../../stringtools.h"

#include "fuse.h"

extern "C"
{
#include <ntifs.h>
#include <device.h>
#include "../../external/btrfs/src/btrfsioctl.h"
#include "get_chunks.h"

    NTSTATUS __stdcall DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath);
    extern PDEVICE_OBJECT master_devobj;

    typedef struct _DUPLICATE_EXTENTS_DATA {
        HANDLE FileHandle;
        LARGE_INTEGER SourceFileOffset;
        LARGE_INTEGER TargetFileOffset;
        LARGE_INTEGER ByteCount;
    } DUPLICATE_EXTENTS_DATA, * PDUPLICATE_EXTENTS_DATA;

#define FSCTL_DUPLICATE_EXTENTS_TO_FILE CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 209, METHOD_BUFFERED, FILE_WRITE_ACCESS)

    typedef struct {
        void* table;
        void* unk1;
        WCHAR* string;
    } DSTRING;

    typedef struct {
        void* table;
    } STREAM_MESSAGE;

    typedef struct {
        uint16_t unk1;
        uint16_t unk2;
        uint32_t flags;
        DSTRING* label;
    } options;

    BOOL __stdcall FormatEx(DSTRING* root, STREAM_MESSAGE* message, options* opts, uint32_t unk1);

#define FORMAT_FLAG_QUICK_FORMAT        0x00000001
}

namespace
{
    std::vector<PDEVICE_OBJECT> new_disks;
    std::unique_ptr<DRIVER_OBJECT> driver_object;

    std::wstring ConvertToWchar(const std::string& input)
    {
        if (input.empty())
        {
            return std::wstring();
        }

        std::wstring ret;
        try
        {
            if (sizeof(wchar_t) == 2)
            {
                utf8::utf8to16(&input[0], &input[input.size() - 1] + 1, back_inserter(ret));
            }
            else if (sizeof(wchar_t) == 4)
            {
                utf8::utf8to32(&input[0], &input[input.size() - 1] + 1, back_inserter(ret));
            }

        }
        catch (...) {}
        return ret;
    }

    std::string ConvertFromWchar(const std::wstring& input)
    {
        if (input.empty())
        {
            return std::string();
        }

        std::string ret;
        try
        {
            if (sizeof(wchar_t) == 2)
            {
                utf8::utf16to8(&input[0], &input[input.size() - 1] + 1, back_inserter(ret));
            }
            else if (sizeof(wchar_t) == 4)
            {
                utf8::utf32to8(&input[0], &input[input.size() - 1] + 1, back_inserter(ret));
            }

        }
        catch (...) {}
        return ret;
    }
}

void register_new_disk(PDEVICE_OBJECT disk)
{
    new_disks.push_back(disk);
}

class UStr
{
public:
    UStr(const std::wstring& wstr)
    {
        str.Buffer = new wchar_t[wstr.size() + 1];
        str.Length = wstr.size()*sizeof(wchar_t);
        str.MaximumLength = (wstr.size() + 1)*sizeof(wchar_t);
        memcpy(str.Buffer, wstr.data(), str.MaximumLength);
    }

    ~UStr()
    {
        delete[] str.Buffer;
    }

    PUNICODE_STRING getUstr()
    {
        return &str;
    }

private:
    UNICODE_STRING str;
};

BtrfsFuse* open_disk_image(const std::string& path)
{
    return nullptr;
}

struct FsData
{
    std::string device_path;
    IFile* img;
    std::unique_ptr<DEVICE_OBJECT> device_object;
    std::unique_ptr<FILE_OBJECT> file_object;
    PDEVICE_OBJECT btrfs_device;
    PFILE_OBJECT root_file;
    PDEVICE_OBJECT fs;
};


void btrfs_fuse_init()
{
    init_windef();

    driver_object = std::make_unique<DRIVER_OBJECT>();
    driver_object->DriverExtension = new DRIVER_OBJECT_EXTENSION;
    UStr reg_path(L"FOOBAR\\BAR");
    DriverEntry(driver_object.get(), reg_path.getUstr());
}

extern std::atomic<size_t> max_volumes;

BtrfsFuse::BtrfsFuse(IFile* img)
    : fs_data(new FsData), img(img)
{
    fs_data->device_path = img->getFilename();
    fs_data->img = img;

    fs_data->device_object = std::make_unique<DEVICE_OBJECT>();
    fs_data->file_object = std::make_unique<FILE_OBJECT>();

    fs_data->device_object->DiskDevice = InitDiskDevice(img, img->Size(), 1);

    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    std::vector<char> moundev_name_buf;
    size_t curr_vol_idx = max_volumes.fetch_add(1);
    std::wstring volname = L"VOL"+std::to_wstring(curr_vol_idx);
    IoRegisterDeviceObjectPointer(UStr(volname).getUstr(), fs_data->file_object.get(), fs_data->device_object.get());
    moundev_name_buf.resize(sizeof(ULONG) + sizeof(WCHAR) * (volname.size() + 1));
    PMOUNTDEV_NAME mountdev_name = reinterpret_cast<PMOUNTDEV_NAME>(moundev_name_buf.data());
    mountdev_name->NameLength = volname.size() * sizeof(WCHAR);
    memcpy(mountdev_name->Name, volname.data(), volname.size() * sizeof(WCHAR));
    irp->AssociatedIrp.SystemBuffer = mountdev_name;
    irp->StackLocation.Parameters.DeviceIoControl.IoControlCode = IOCTL_BTRFS_PROBE_VOLUME;
    irp->StackLocation.Parameters.DeviceIoControl.InputBufferLength = moundev_name_buf.size();
    driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL](master_devobj, irp.get());

    if (new_disks.empty())
    {
        has_error = true;
        return;
    }

    PDEVICE_OBJECT btrfs_disk = new_disks[new_disks.size() - 1];
    for (PDEVICE_OBJECT disk : new_disks)
    {
        driver_object->DriverExtension->AddDevice(driver_object.get(), disk);
        fs_data->btrfs_device = disk->AttachedDevice;
    }
    new_disks.clear();

    irp->StackLocation.MinorFunction = IRP_MN_MOUNT_VOLUME;
    irp->StackLocation.Parameters.MountVolume.DeviceObject = btrfs_disk;
    PVPB vpb = new VPB;
    irp->StackLocation.Parameters.MountVolume.Vpb = vpb;
    vpb->DeviceObject = btrfs_disk;
    vpb->RealDevice = fs_data->device_object.get();
    driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](master_devobj, irp.get());
    fs_data->fs = vpb->DeviceObject;

    PFILE_OBJECT tmp_root_file = FsRtlNotifyGetLastVolumeEventsubject();

    fs_data->root_file = nullptr;
    fs_data->root_file = openFileInt("\\", MODE_READ, false, false).release();

    /*fs_data->root_file = FsRtlNotifyGetLastVolumeEventsubject();
    fs_data->root_file->RelatedFileObject = NULL;
    UStr root_path(L"\\");
    fs_data->root_file->FileName = *root_path.getUstr();
    

    irp = std::make_unique<IRP>();
    std::unique_ptr<SECURITY_CONTEXT> security_context = std::make_unique<SECURITY_CONTEXT>();
    std::unique_ptr<ACCESS_STATE> access_state = std::make_unique< ACCESS_STATE>();
    security_context->AccessState = access_state.get();
    irp->StackLocation.MajorFunction = IRP_MJ_CREATE;
    irp->StackLocation.MinorFunction = IRP_MN_NORMAL;
    irp->StackLocation.FileObject = fs_data->root_file;
    irp->StackLocation.Parameters.Create.Options = FILE_OPEN << 24;
    irp->StackLocation.Parameters.Create.SecurityContext = security_context.get();
    driver_object->MajorFunction[IRP_MJ_CREATE](fs_data->fs, irp.get());*/
}

BtrfsFuse::~BtrfsFuse()
{
    Server->destroy(fs_data->img);
}

bool BtrfsFuse::createDir(const std::string& path)
{
    std::unique_ptr<_FILE_OBJECT> file_obj = openFileInt(path, MODE_WRITE, true, false);

    if (file_obj.get() == nullptr)
        return false;

    return closeFile(std::move(file_obj));
}

bool BtrfsFuse::deleteFile(const std::string& path)
{
    std::unique_ptr<_FILE_OBJECT> file_obj = openFileInt(path, MODE_RW, false, true);

    if (file_obj.get() == nullptr)
        return false;

    return closeFile(std::move(file_obj));
}

class BtrfsFuseFile : public IFsFile
{
public:
    BtrfsFuseFile(FsData* fs_data, std::unique_ptr<FILE_OBJECT> file_obj, const std::string& path)
        : fs_data(fs_data), path(path), file_obj(std::move(file_obj)), pos(0)
    {

    }

    ~BtrfsFuseFile()
    {
        std::unique_ptr<IRP> irp = std::make_unique<IRP>();
        irp->StackLocation.MajorFunction = IRP_MJ_CLEANUP;
        irp->StackLocation.MinorFunction = IRP_MN_NORMAL;
        irp->StackLocation.FileObject = file_obj.get();
        NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_CLEANUP](fs_data->fs, irp.get());
        if (!NT_SUCCESS(rc))
        {
            Server->Log("Closing btrfs file \"" + path + "\" failed. Rc=" + std::to_string(rc), LL_ERROR);
            assert(false);
        }

        /*
        std::unique_ptr<IRP> irp = std::make_unique<IRP>();
        irp->StackLocation.MajorFunction = IRP_MJ_CLOSE;
        irp->StackLocation.MinorFunction = IRP_MN_NORMAL;
        irp->StackLocation.FileObject = file_obj.get();
        NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_CLOSE](fs_data->fs, irp.get());
        */
    }

    virtual std::string Read(_u32 tr, bool* has_error = NULL) override
    {
        std::string ret= Read(pos, tr, has_error);
        pos += ret.size();
        return ret;
    }

    virtual std::string Read(int64 spos, _u32 tr, bool* has_error = NULL) override
    {
        std::string str;
        str.resize(tr);

        _u32 read = Read(spos, &str[0], tr, has_error);
        if (read != tr)
            str.resize(read);

        return str;
    }

    virtual _u32 Read(char* buffer, _u32 bsize, bool* has_error = NULL) override
    {
        _u32 read = Read(pos, buffer, bsize, has_error);
        pos += read;
        return read;
    }

    virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool* has_error = NULL) override
    {
        std::unique_ptr<IRP> irp = std::make_unique<IRP>();
        irp->Flags = IRP_NOCACHE;
        irp->StackLocation.MajorFunction = IRP_MJ_READ;
        irp->StackLocation.Parameters.Read.ByteOffset.QuadPart = spos;
        irp->StackLocation.Parameters.Read.Length = bsize;
        irp->StackLocation.FileObject = file_obj.get();
        irp->UserBuffer = buffer;
        NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_READ](fs_data->fs, irp.get());
        if (!NT_SUCCESS(rc) &&
            rc!= STATUS_END_OF_FILE)
        {
            errno = rc;
            if(has_error!=nullptr)
                *has_error = true;
            return 0;
        }

        return static_cast<_u32>(irp->IoStatus.Information);
    }
    virtual _u32 Write(const std::string& tw, bool* has_error = NULL) override
    {
        return Write(tw.data(), tw.size(), has_error);
    }

    virtual _u32 Write(int64 spos, const std::string& tw, bool* has_error = NULL) override
    {
        return Write(spos, tw.data(), tw.size(), has_error);
    }

    virtual _u32 Write(const char* buffer, _u32 bsiz, bool* has_error = NULL) override
    {
        _u32 written = Write(pos, buffer, bsiz, has_error);
        pos += written;
        return written;
    }

    virtual _u32 Write(int64 spos, const char* buffer, _u32 bsize, bool* has_error = NULL) override
    {
        std::unique_ptr<IRP> irp = std::make_unique<IRP>();
        irp->Flags = IRP_NOCACHE;
        irp->StackLocation.MajorFunction = IRP_MJ_WRITE;
        irp->StackLocation.Parameters.Write.ByteOffset.QuadPart = spos;
        irp->StackLocation.Parameters.Write.Length = bsize;
        irp->StackLocation.FileObject = file_obj.get();
        irp->UserBuffer = const_cast<char*>(buffer);
        NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_WRITE](fs_data->fs, irp.get());
        if (!NT_SUCCESS(rc))
        {
            errno = rc;
            if (has_error != nullptr)
                *has_error = true;
            return 0;
        }

        return static_cast<_u32>(irp->IoStatus.Information);
    }

    virtual bool Seek(_i64 spos) override
    {
        pos = spos;
        return true;
    }

    virtual _i64 Size(void) override
    {
        FILE_STANDARD_INFORMATION fsi;
        std::unique_ptr<IRP> irp = std::make_unique<IRP>();
        irp->Flags = IRP_NOCACHE;
        irp->StackLocation.MajorFunction = IRP_MJ_QUERY_INFORMATION;
        irp->StackLocation.Parameters.QueryFile.FileInformationClass = FileStandardInformation;
        irp->StackLocation.Parameters.QueryFile.Length = sizeof(fsi);
        irp->StackLocation.FileObject = file_obj.get();
        irp->AssociatedIrp.SystemBuffer = &fsi;
        NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_QUERY_INFORMATION](fs_data->fs, irp.get());
        if (!NT_SUCCESS(rc))
        {
            return -1;
        }
        return fsi.EndOfFile.QuadPart;
    }

    virtual _i64 RealSize() override
    {
        return Size();
    }

    virtual bool PunchHole(_i64 spos, _i64 size) override
    {
        return false;
    }

    virtual bool Sync() override
    {
        return false;
    }

    virtual std::string getFilename(void) override
    {
        return path;
    }

    virtual void resetSparseExtentIter() override
    {
    }

    virtual SSparseExtent nextSparseExtent() override
    {
        return SSparseExtent();
    }
    virtual bool Resize(int64 new_size, bool set_sparse = true) override
    {
        std::unique_ptr<IRP> irp = std::make_unique<IRP>();
        irp->Flags = IRP_NOCACHE;
        irp->StackLocation.MajorFunction = IRP_MJ_SET_INFORMATION;
        irp->StackLocation.Parameters.SetFile.FileInformationClass = FileAllocationInformation;
        FILE_END_OF_FILE_INFORMATION eofi;
        eofi.EndOfFile.QuadPart = new_size;
        irp->StackLocation.Parameters.SetFile.Length = sizeof(eofi);
        irp->StackLocation.FileObject = file_obj.get();
        irp->AssociatedIrp.SystemBuffer = &eofi;
        NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_SET_INFORMATION](fs_data->fs, irp.get());
        if (!NT_SUCCESS(rc))
        {
            return false;
        }
        
        return true;
    }
    virtual std::vector<SFileExtent> getFileExtents(int64 starting_offset, int64 block_size, bool& more_data, unsigned int flags) override
    {
        return std::vector<SFileExtent>();
    }
    virtual IVdlVolCache* createVdlVolCache() override
    {
        return nullptr;
    }
    virtual int64 getValidDataLength(IVdlVolCache* vol_cache) override
    {
        return int64();
    }
    virtual os_file_handle getOsHandle(bool release_handle = false) override
    {
        return os_file_handle();
    }

private:
    FsData* fs_data;
    std::string path;
    std::unique_ptr<_FILE_OBJECT> file_obj;
    int64 pos;
};

IFsFile* BtrfsFuse::openFile(const std::string& path, int mode)
{
    std::unique_ptr<_FILE_OBJECT> file_obj = openFileInt(path, mode, false, false);

    if (file_obj.get() == nullptr)
        return nullptr;
    
    return new BtrfsFuseFile(fs_data.get(), std::move(file_obj), path);
}

int BtrfsFuse::getFileType(const std::string& path)
{
    std::unique_ptr<_FILE_OBJECT> file_obj = openFileInt(path, MODE_READ, false, false);

    if (file_obj.get() == nullptr)
        return 0;

    int ret = 0;

    FILE_STANDARD_INFORMATION fsi;
    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.MajorFunction = IRP_MJ_QUERY_INFORMATION;
    irp->StackLocation.Parameters.QueryFile.FileInformationClass = FileStandardInformation;
    irp->StackLocation.Parameters.QueryFile.Length = sizeof(fsi);
    irp->StackLocation.FileObject = file_obj.get();
    irp->AssociatedIrp.SystemBuffer = &fsi;
    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_QUERY_INFORMATION](fs_data->fs, irp.get());
    if (NT_SUCCESS(rc))
    {
        if (fsi.Directory)
        {
            ret = EFileType_Directory;
        }
        else
        {
            ret = EFileType_File;
        }
    }

    closeFile(std::move(file_obj));

    return ret;
}

bool BtrfsFuse::reflink(const std::string& src_path, const std::string& dest_path)
{
    std::unique_ptr<_FILE_OBJECT> dest_file_obj = openFileInt(dest_path, MODE_WRITE, false, false);

    if (dest_file_obj.get() == nullptr)
        return false;

    std::unique_ptr<_FILE_OBJECT> src_file_obj = openFileInt(src_path, MODE_READ, false, false);

    if (src_file_obj.get() == nullptr)
        return false;

    int64 source_size = fileSize(src_file_obj.get());
    if (source_size < 0)
        return false;

    if (source_size == 0)
        return true;

    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_USER_FS_REQUEST;
    irp->StackLocation.Parameters.FileSystemControl.FsControlCode = FSCTL_DUPLICATE_EXTENTS_TO_FILE;
    DUPLICATE_EXTENTS_DATA ded;
    irp->AssociatedIRP = irp.get();
    irp->UserBuffer = &ded;
    irp->StackLocation.Parameters.FileSystemControl.InputBufferLength = sizeof(ded);
    irp->StackLocation.Parameters.FileSystemControl.OutputBufferLength = 0;
    irp->StackLocation.FileObject = dest_file_obj.get();

    HANDLE hSrc = ObRegisterHandle(src_file_obj.get());
    ded.FileHandle = hSrc;
    ded.SourceFileOffset.QuadPart = 0;
    ded.TargetFileOffset.QuadPart = 0;
    ded.ByteCount.QuadPart = source_size;

    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](fs_data->fs, irp.get());
    bool ret = false;
    if (NT_SUCCESS(rc))
    {
        ret = true;
    }

    ObDeregisterHandle(hSrc);

    closeFile(std::move(dest_file_obj));
    closeFile(std::move(src_file_obj));

    return ret;
}

bool BtrfsFuse::flush()
{
    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.MajorFunction = IRP_MJ_POWER;
    irp->StackLocation.MinorFunction = IRP_MN_QUERY_POWER;
    irp->StackLocation.Parameters.Power.Type = SystemPowerState;
    irp->StackLocation.Parameters.Power.State.SystemState = PowerSystemNone;
    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_POWER](fs_data->btrfs_device, irp.get());
    if (!NT_SUCCESS(rc))
    {
        return false;
    }

    return true;
}

std::vector<SFile> BtrfsFuse::listFiles(const std::string& path)
{
    std::unique_ptr<_FILE_OBJECT> file_obj;
    
    if (!path.empty())
    {
        file_obj = openFileInt(path, MODE_READ, true, false);

        if (file_obj.get() == nullptr)
            return std::vector<SFile>();
    }

    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->StackLocation.MajorFunction = IRP_MJ_DIRECTORY_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_QUERY_DIRECTORY;

    if (file_obj.get() != nullptr)
        irp->StackLocation.FileObject = file_obj.get();
    else
        irp->StackLocation.FileObject = fs_data->root_file;

    irp->StackLocation.Flags = SL_RESTART_SCAN;
    std::vector<char> out_buf;
    out_buf.resize(1024);
    irp->UserBuffer = out_buf.data();
    irp->StackLocation.Parameters.QueryDirectory.Length = out_buf.size();
    irp->StackLocation.Parameters.QueryDirectory.FileInformationClass = FileBothDirectoryInformation;
    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_DIRECTORY_CONTROL](fs_data->fs, irp.get());

    if (rc != STATUS_SUCCESS)
    {
        closeFile(std::move(file_obj));
        errno = rc;
        return std::vector<SFile>();
    }

    std::vector<SFile> ret;
    for (size_t i = 0; i + sizeof(FILE_BOTH_DIR_INFORMATION) - sizeof(WCHAR) < irp->IoStatus.Information;)
    {
        FILE_BOTH_DIR_INFORMATION* fni = reinterpret_cast<FILE_BOTH_DIR_INFORMATION*>(out_buf.data() + i);

        SFile cf;
        cf.name = ConvertFromWchar(std::wstring(fni->FileName, fni->FileNameLength / sizeof(WCHAR)));
        if (cf.name.find("\\dirty")!=std::string::npos)
            int abc = 5;
        cf.size = fni->EndOfFile.QuadPart;
        cf.created = fni->CreationTime.QuadPart;
        cf.accessed = fni->LastAccessTime.QuadPart;
        cf.last_modified = fni->LastWriteTime.QuadPart;
        cf.isdir = (fni->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) > 0;
        cf.issym = (fni->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) > 0;

        if (cf.name != "." && cf.name != "..")
            ret.push_back(cf);

        if (fni->NextEntryOffset == 0)
        {
            irp->StackLocation.Flags = 0;
            
            rc = driver_object->MajorFunction[IRP_MJ_DIRECTORY_CONTROL](fs_data->fs, irp.get());

            if (rc == STATUS_NO_MORE_FILES)
            {
                break;
            }

            if (rc != STATUS_SUCCESS)
            {
                closeFile(std::move(file_obj));
                errno = rc;
                return std::vector<SFile>();
            }

            i = 0;
            continue;
        }

        i += fni->NextEntryOffset;
    }

    closeFile(std::move(file_obj));
    errno = 0;
    return ret;
}

bool BtrfsFuse::create_subvol(const std::string& path)
{
    std::wstring path_w = ConvertToWchar(path);
    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_USER_FS_REQUEST;
    irp->StackLocation.Parameters.FileSystemControl.FsControlCode = FSCTL_BTRFS_CREATE_SUBVOL;
    std::vector<char> bcs_buf(sizeof(btrfs_create_subvol) + path_w.size()*sizeof(wchar_t));
    btrfs_create_subvol* bcs = reinterpret_cast<btrfs_create_subvol*>(bcs_buf.data());
    irp->AssociatedIRP = irp.get();
    irp->UserBuffer = bcs;
    irp->StackLocation.Parameters.FileSystemControl.InputBufferLength = bcs_buf.size();
    irp->StackLocation.Parameters.FileSystemControl.OutputBufferLength = 0;
    irp->StackLocation.FileObject = fs_data->root_file;
    irp->AssociatedIrp.SystemBuffer = bcs;

    memcpy(bcs->name, path_w.data(), path_w.size() * sizeof(WCHAR));
    bcs->namelen = path_w.size()*sizeof(WCHAR);
    bcs->posix = false;
    bcs->readonly = false;

    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](fs_data->fs, irp.get());
    
    if (!NT_SUCCESS(rc))
    {
        return false;
    }

    return true;
}

bool BtrfsFuse::create_snapshot(const std::string& src_path, const std::string& dest_path)
{
    std::unique_ptr<_FILE_OBJECT> src_file_obj = openFileInt(src_path, MODE_READ, true, false);

    if (src_file_obj.get() == nullptr)
        return false;

    std::wstring dest_path_w = ConvertToWchar(dest_path);

    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_USER_FS_REQUEST;
    irp->StackLocation.Parameters.FileSystemControl.FsControlCode = FSCTL_BTRFS_CREATE_SNAPSHOT;
    std::vector<char> bcs_buf(sizeof(btrfs_create_snapshot) + dest_path_w.size()*sizeof(wchar_t));
    btrfs_create_snapshot* bcs = reinterpret_cast<btrfs_create_snapshot*>(bcs_buf.data());
    irp->AssociatedIRP = irp.get();
    irp->UserBuffer = bcs;
    irp->StackLocation.Parameters.FileSystemControl.InputBufferLength = bcs_buf.size();
    irp->StackLocation.Parameters.FileSystemControl.OutputBufferLength = 0;
    irp->StackLocation.FileObject = fs_data->root_file;
    irp->AssociatedIrp.SystemBuffer = bcs;

    HANDLE hSrc = ObRegisterHandle(src_file_obj.get());
    bcs->subvol = hSrc;
    memcpy(bcs->name, dest_path_w.data(), dest_path_w.size() * sizeof(WCHAR));
    bcs->namelen = dest_path_w.size()*sizeof(WCHAR);
    bcs->posix = false;
    bcs->readonly = false;

    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](fs_data->fs, irp.get());
    bool ret = false;
    if (NT_SUCCESS(rc))
    {
        ret = true;
    }

    ObDeregisterHandle(hSrc);

    closeFile(std::move(src_file_obj));

    return ret;
}

bool BtrfsFuse::rename(const std::string& orig_name, const std::string& new_name)
{
    std::unique_ptr<_FILE_OBJECT> src_file_obj = openFileInt(orig_name, MODE_READ, false, false);

    if (src_file_obj.get() == nullptr)
    {
        src_file_obj = openFileInt(orig_name, MODE_READ, true, false);
        if (src_file_obj.get() == nullptr)
            return false;
    }

    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.MajorFunction = IRP_MJ_SET_INFORMATION;
    irp->StackLocation.Parameters.SetFile.FileInformationClass = FileRenameInformation;


    //HANDLE root_handle = ObRegisterHandle(fs_data->root_file);

    std::wstring new_name_w;

    if (ExtractFilePath(orig_name, "\\") == ExtractFilePath(new_name, "\\"))
    {
        new_name_w = ConvertToWchar(ExtractFileName(new_name, "\\"));
    }
    else
    {
        new_name_w = L"\\" + ConvertToWchar(new_name);
    }


    std::vector<char> buf(sizeof(FILE_RENAME_INFORMATION) + new_name_w.size() * sizeof(wchar_t));
    FILE_RENAME_INFORMATION* fri = reinterpret_cast<FILE_RENAME_INFORMATION*>(buf.data());
    fri->RootDirectory = nullptr;
    fri->ReplaceIfExists = TRUE;
    fri->Flags = 0;
    fri->FileNameLength = new_name_w.size()*sizeof(WCHAR);
    memcpy(fri->FileName, new_name_w.data(), fri->FileNameLength);
    
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.Parameters.SetFile.Length = buf.size();
    irp->StackLocation.FileObject = src_file_obj.get();
    irp->AssociatedIrp.SystemBuffer = fri;
    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_SET_INFORMATION](fs_data->fs, irp.get());

    bool ret = false;
    if (NT_SUCCESS(rc))
    {
        ret = true;
    }

    //ObDeregisterHandle(root_handle);

    closeFile(std::move(src_file_obj));

    return ret;
}

bool BtrfsFuse::link_symbolic(const std::string& target, const std::string& lname)
{
    std::unique_ptr<_FILE_OBJECT> lname_obj = openFileInt(lname, MODE_WRITE, false, false);

    if (lname_obj.get() == nullptr)
        return false;

    std::wstring target_w = ConvertToWchar(target);
    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_USER_FS_REQUEST;
    irp->StackLocation.Parameters.FileSystemControl.FsControlCode = FSCTL_SET_REPARSE_POINT;
    std::vector<char> rdb_buf(offsetof(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer) + target_w.size() + target_w.size());
    REPARSE_DATA_BUFFER* rdb = reinterpret_cast<REPARSE_DATA_BUFFER*>(rdb_buf.data());
    irp->AssociatedIRP = irp.get();
    irp->UserBuffer = rdb;
    irp->StackLocation.Parameters.FileSystemControl.InputBufferLength = rdb_buf.size();
    irp->StackLocation.Parameters.FileSystemControl.OutputBufferLength = 0;
    irp->StackLocation.FileObject = lname_obj.get();
    irp->AssociatedIrp.SystemBuffer = rdb;

    rdb->ReparseTag = IO_REPARSE_TAG_SYMLINK;
    rdb->ReparseDataLength = static_cast<USHORT>(rdb_buf.size() - offsetof(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer));
    rdb->Reserved = 0;

    rdb->SymbolicLinkReparseBuffer.SubstituteNameOffset = 0;
    rdb->SymbolicLinkReparseBuffer.SubstituteNameLength = target_w.size();
    rdb->SymbolicLinkReparseBuffer.PrintNameOffset = target_w.size();
    rdb->SymbolicLinkReparseBuffer.PrintNameLength = target_w.size();
    rdb->SymbolicLinkReparseBuffer.Flags = SYMLINK_FLAG_RELATIVE;

    memcpy(rdb->SymbolicLinkReparseBuffer.PathBuffer, target_w.data(), target_w.size() * sizeof(WCHAR));
    memcpy(rdb->SymbolicLinkReparseBuffer.PathBuffer+target_w.size(), target_w.data(), target_w.size() * sizeof(WCHAR));

    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](fs_data->fs, irp.get());

    bool ret = false;
    if (NT_SUCCESS(rc))
    {
        ret = true;
    }

    closeFile(std::move(lname_obj));

    return ret;    
}

bool BtrfsFuse::get_has_error()
{
    return has_error;
}

bool BtrfsFuse::resize_max()
{
    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_USER_FS_REQUEST;
    irp->StackLocation.Parameters.FileSystemControl.FsControlCode = FSCTL_BTRFS_RESIZE;
    std::vector<char> bcs_buf(sizeof(btrfs_resize));
    btrfs_resize* bcs = reinterpret_cast<btrfs_resize*>(bcs_buf.data());
    irp->AssociatedIRP = irp.get();
    irp->UserBuffer = bcs;
    irp->StackLocation.Parameters.FileSystemControl.InputBufferLength = bcs_buf.size();
    irp->StackLocation.Parameters.FileSystemControl.OutputBufferLength = 0;
    irp->StackLocation.FileObject = fs_data->root_file;
    irp->AssociatedIrp.SystemBuffer = bcs;

    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](fs_data->fs, irp.get());

    if (!NT_SUCCESS(rc))
    {
        return false;
    }
    
    return true;
}

BtrfsFuse::SpaceInfo BtrfsFuse::get_space_info()
{
    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_USER_FS_REQUEST;
    irp->StackLocation.Parameters.FileSystemControl.FsControlCode = FSCTL_BTRFS_GET_USAGE;
    std::vector<char> bcs_buf(sizeof(btrfs_usage)*20);
    btrfs_usage* bcs = reinterpret_cast<btrfs_usage*>(bcs_buf.data());
    irp->AssociatedIRP = irp.get();
    irp->UserBuffer = bcs;
    irp->StackLocation.Parameters.FileSystemControl.InputBufferLength = 0;
    irp->StackLocation.Parameters.FileSystemControl.OutputBufferLength = bcs_buf.size();
    irp->StackLocation.FileObject = fs_data->root_file;
    irp->AssociatedIrp.SystemBuffer = bcs;

    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](fs_data->fs, irp.get());

    if (!NT_SUCCESS(rc))
    {
        return BtrfsFuse::SpaceInfo();
    }

    BtrfsFuse::SpaceInfo ret = {};

    size_t coffset = 0;
    while (bcs != nullptr &&
        coffset + sizeof(btrfs_usage )<= bcs_buf.size())
    {
        bcs = reinterpret_cast<btrfs_usage*>(bcs_buf.data() + coffset);

        if (bcs->type & BLOCK_FLAG_METADATA)
        {
            ret.metadata_allocated += bcs->size;
            ret.metadata_used += bcs->used;
        }
        if (bcs->type & BLOCK_FLAG_DATA)
        {
            ret.data_allocated += bcs->size;
            ret.data_used += bcs->used;
        }
        
        ret.used += bcs->used;
        ret.allocated += bcs->size;

        if (bcs->next_entry == 0)
            break;

        coffset += bcs->next_entry;
    }

    ret.unallocated = img->Size() - ret.allocated;

    return ret;    
}

int64 BtrfsFuse::get_total_space()
{
    return img->Size();
}

str_map BtrfsFuse::get_xattrs(const std::string& path)
{
    std::unique_ptr<_FILE_OBJECT> src_file_obj = openFileInt(path, MODE_READ, false, false);

    if (src_file_obj.get() == nullptr)
        return str_map();

    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_USER_FS_REQUEST;
    irp->StackLocation.Parameters.FileSystemControl.FsControlCode = FSCTL_BTRFS_GET_XATTRS;
    std::vector<char> bcs_buf(1024);
    btrfs_set_xattr* bcs = reinterpret_cast<btrfs_set_xattr*>(bcs_buf.data());
    irp->AssociatedIRP = irp.get();
    irp->UserBuffer = bcs;
    irp->StackLocation.Parameters.FileSystemControl.InputBufferLength = bcs_buf.size();
    irp->StackLocation.Parameters.FileSystemControl.OutputBufferLength = 0;
    irp->StackLocation.FileObject = src_file_obj.get();
    irp->AssociatedIrp.SystemBuffer = bcs;

    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](fs_data->fs, irp.get());

    if (!NT_SUCCESS(rc))
    {
        return str_map();
    }

    str_map ret;
    for (size_t coffset = 0; coffset < irp->IoStatus.Information;)
    {
        bcs = reinterpret_cast<btrfs_set_xattr*>(bcs_buf.data() + coffset);

        std::string k(bcs->data, bcs->namelen);
        std::string v(bcs->data + bcs->namelen, bcs->valuelen);
        ret[k] = v;
    }
    return ret;
}

bool BtrfsFuse::set_xattr(const std::string& path, const std::string& tkey, const std::string& tval)
{
    std::unique_ptr<_FILE_OBJECT> src_file_obj = openFileInt(path, MODE_RW, false, false);

    if (src_file_obj.get() == nullptr)
        return false;

    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    irp->StackLocation.MinorFunction = IRP_MN_USER_FS_REQUEST;
    irp->StackLocation.Parameters.FileSystemControl.FsControlCode = FSCTL_BTRFS_SET_XATTR;
    std::vector<char> bcs_buf(sizeof(btrfs_set_xattr)+tkey.size()+tval.size());
    btrfs_set_xattr* bcs = reinterpret_cast<btrfs_set_xattr*>(bcs_buf.data());
    irp->AssociatedIRP = irp.get();
    irp->UserBuffer = bcs;
    irp->StackLocation.Parameters.FileSystemControl.InputBufferLength = bcs_buf.size();
    irp->StackLocation.Parameters.FileSystemControl.OutputBufferLength = 0;
    irp->StackLocation.FileObject = src_file_obj.get();
    irp->AssociatedIrp.SystemBuffer = bcs;


    bcs->namelen = tkey.size();
    bcs->valuelen = tval.size();
    memcpy(bcs->data, tkey.data(), tkey.size());
    memcpy(bcs->data + tkey.size(), tval.data(), tval.size());

    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL](fs_data->fs, irp.get());

    if (!NT_SUCCESS(rc))
    {
        return false;
    }

    return true;
}

std::string BtrfsFuse::errno_to_str(int rc)
{
    switch (rc)
    {
    case STATUS_SUCCESS:
        return "Success";
    case STATUS_INVALID_PARAMETER:
        return "Invalid parameter";
    case STATUS_ACCESS_DENIED:
        return "Access denied";
    case STATUS_NOT_FOUND:
        return "Not found";
    case STATUS_CRC_ERROR:
        return "Crc error";
    case STATUS_UNEXPECTED_IO_ERROR:
        return "Unexpected IO error";
    case STATUS_END_OF_FILE:
        return "End of file";
    case STATUS_OBJECT_NAME_COLLISION:
        return "Object name collision";
    case STATUS_FILE_IS_A_DIRECTORY:
        return "File is a directory";
    case STATUS_NOT_A_DIRECTORY:
        return "Not a directory";
    case STATUS_DIRECTORY_NOT_EMPTY:
        return "Directory not empty";
    case STATUS_NO_MORE_ENTRIES:
        return "No more entries";
    case STATUS_NOT_A_REPARSE_POINT:
        return "Not a reparse point";
    default:
        return "Untrans - " + std::to_string(rc);
    }
}

std::pair<BtrfsFuse::SBtrfsChunk*, size_t> BtrfsFuse::get_chunks()
{
    size_t nchunks;
    SBtrfsChunk* chunks = reinterpret_cast<SBtrfsChunk*>(
        get_btrfs_chunks(reinterpret_cast<device_extension*>(fs_data->fs->DeviceExtension), &nchunks));

    return std::make_pair(chunks, nchunks);
}

std::unique_ptr<_FILE_OBJECT> BtrfsFuse::openFileInt(const std::string& path, int mode, bool openDirectory, bool deleteFile)
{
    if (path == "trans_4\\s\\7300")
        int abc = 5;
    if (mode == MODE_WRITE || mode== MODE_RW_CREATE)
    {
        this->deleteFile(path);
    }
    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    std::unique_ptr<SECURITY_CONTEXT> security_context = std::make_unique<SECURITY_CONTEXT>();
    std::unique_ptr<ACCESS_STATE> access_state = std::make_unique< ACCESS_STATE>();
    security_context->AccessState = access_state.get();
    irp->StackLocation.MajorFunction = IRP_MJ_CREATE;
    irp->StackLocation.MinorFunction = IRP_MN_NORMAL;
    irp->StackLocation.Flags = SL_CASE_SENSITIVE;
    irp->StackLocation.Parameters.Create.Options = FILE_OPEN << 24;
    if (mode == MODE_WRITE || mode==MODE_RW_CREATE)
    {
        irp->StackLocation.Parameters.Create.Options |= (FILE_CREATE << 24);
    }
    if (deleteFile)
    {
        irp->StackLocation.Parameters.Create.Options |= FILE_DELETE_ON_CLOSE;
    }
    if (openDirectory)
    {
        irp->StackLocation.Parameters.Create.Options |= FILE_DIRECTORY_FILE;
    }
    irp->StackLocation.Parameters.Create.SecurityContext = security_context.get();

    ACCESS_MASK desired_access = READ_CONTROL | SYNCHRONIZE | ACCESS_SYSTEM_SECURITY | FILE_READ_DATA |
        FILE_READ_EA | FILE_READ_ATTRIBUTES | FILE_EXECUTE | FILE_LIST_DIRECTORY |
        FILE_TRAVERSE | DELETE | WRITE_OWNER | WRITE_DAC | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES;

    if(openDirectory)
        desired_access |= FILE_ADD_SUBDIRECTORY | FILE_ADD_FILE | FILE_DELETE_CHILD;

    security_context->DesiredAccess = desired_access;
    std::unique_ptr<_FILE_OBJECT> file_obj = std::make_unique<_FILE_OBJECT>();
    file_obj->RelatedFileObject = fs_data->root_file;
    UStr test_file_path(ConvertToWchar(path));
    file_obj->FileName = *test_file_path.getUstr();
    irp->StackLocation.FileObject = file_obj.get();
    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_CREATE](fs_data->fs, irp.get());

    if (rc != 0)
    {
        errno = rc;
        return std::unique_ptr<_FILE_OBJECT>();
    }

    return std::move(file_obj);
}

bool BtrfsFuse::closeFile(std::unique_ptr<_FILE_OBJECT> file_object)
{
    return closeFile(file_object.get());
}

bool BtrfsFuse::closeFile(PFILE_OBJECT file_object)
{
    if (file_object == nullptr)
        return true;

    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    /*irp->StackLocation.MajorFunction = IRP_MJ_CLOSE;
    irp->StackLocation.MinorFunction = IRP_MN_NORMAL;
    irp->StackLocation.FileObject = file_object;
    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_CLOSE](fs_data->fs, irp.get());
    if (!NT_SUCCESS(rc))
    {
        errno = rc;
        return false;
    }*/

    irp->StackLocation.MajorFunction = IRP_MJ_CLEANUP;
    irp->StackLocation.MinorFunction = IRP_MN_NORMAL;
    irp->StackLocation.FileObject = file_object;
    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_CLEANUP](fs_data->fs, irp.get());
    if (!NT_SUCCESS(rc))
    {
        errno = rc;
        return false;
    }

    return true;
}

int64 BtrfsFuse::fileSize(PFILE_OBJECT file_object)
{
    FILE_STANDARD_INFORMATION fsi;
    std::unique_ptr<IRP> irp = std::make_unique<IRP>();
    irp->Flags = IRP_NOCACHE;
    irp->StackLocation.MajorFunction = IRP_MJ_QUERY_INFORMATION;
    irp->StackLocation.Parameters.QueryFile.FileInformationClass = FileStandardInformation;
    irp->StackLocation.Parameters.QueryFile.Length = sizeof(fsi);
    irp->StackLocation.FileObject = file_object;
    irp->AssociatedIrp.SystemBuffer = &fsi;
    NTSTATUS rc = driver_object->MajorFunction[IRP_MJ_QUERY_INFORMATION](fs_data->fs, irp.get());
    if (!NT_SUCCESS(rc))
    {
        return -1;
    }
    return fsi.EndOfFile.QuadPart;
}

bool btrfs_format_vol(IFile* vol)
{
    static std::atomic<int> file_num;
    std::wstring file_name = L"vol" + std::to_wstring(file_num.fetch_add(1));
    RegisterNtFile(&file_name[0], vol);

    DSTRING root = { nullptr, nullptr, &file_name[0] };

    STREAM_MESSAGE message = { nullptr };

    DSTRING label = { nullptr, nullptr, &file_name[0] };

    options opts = { 0, 0, FORMAT_FLAG_QUICK_FORMAT, &label };

    BOOL ret = FormatEx(&root, &message, &opts, 0);

    UnregisterNtFile(&file_name[0]);

    return ret;
}