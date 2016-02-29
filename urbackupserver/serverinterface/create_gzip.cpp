#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/param.h>
#include <stdlib.h>
#include <archive_entry.h>
#include <archive.h>

#include "action_header.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../Interface/File.h"


static bool write_file_content_to_archive(struct archive *archive,
                                          const std::wstring &fileName)
{
    FILE *file = fopen(Server->ConvertToUTF8(fileName).c_str(), "r");
    if (!file) {
        Server->Log(L"Error opening file \""+
                    fileName, LL_ERROR);
        return false;
    }
    fseek(file, 0, SEEK_END);
    long int fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    while (!feof(file)) {
        unsigned char fileBuf[1024 * 8];
        size_t rc = fread(fileBuf, 1, sizeof(fileBuf), file);
        if (ferror(file)) {
            Server->Log(L"Error reading file \""+
                    fileName, LL_ERROR);
            fclose(file);
            return false;
        }
        archive_write_data(archive, fileBuf, rc);
    }
    fclose(file);
    return true;
}

static bool load_archive_file_entry(struct archive_entry *entry,
                                    const std::wstring &fileName,
                                    const std::wstring &entryName)
{
    struct stat st;
    archive_entry_clear(entry);
    if (stat(Server->ConvertToUTF8(fileName).c_str(), &st) != 0) {
        Server->Log(L"stat() returned error for file \"" +
                    fileName + L"\": " + convert(strerror(errno)), LL_ERROR);
        return false;
    }
    archive_entry_copy_stat(entry, &st);
    archive_entry_set_pathname(entry, Server->ConvertToUTF8(entryName).c_str());
    if ((st.st_mode & S_IFMT) == S_IFREG) {
        /* For regular files */
        archive_entry_set_size(entry, st.st_size);
        archive_entry_set_perm(entry, 0644);
    }
    else {
        /* For directories */
        archive_entry_set_size(entry, 0);
        archive_entry_set_perm(entry, 0755);
    }
    archive_entry_set_filetype(entry, st.st_mode & S_IFMT);
    archive_entry_set_uid(entry, 0);
    archive_entry_set_gid(entry, 0);
    return true;
}

static bool traverse_dir(struct archive *archive,
                         struct archive_entry *entry,
                         const std::wstring &archiveFolderName,
                         const std::wstring &folderName,
                         const std::wstring &filter)
{
    bool has_error = false;
    const std::vector<SFile> files =
        getFiles(folderName, &has_error, true, false);
    if (has_error)
        return false;

    for (size_t i = 0; i < files.size(); i ++) {

        const SFile &file = files[i];
        std::wstring entryName = archiveFolderName +
            (archiveFolderName.empty()?L"":L"/") + file.name;
        std::wstring fileName = folderName + os_file_sep() + file.name;
        if (!filter.empty() && entryName != filter)
            continue;

        load_archive_file_entry(entry, fileName, entryName);
        int r = archive_write_header(archive, entry);
        if (r < ARCHIVE_OK) {
            Server->Log("Failed to write header for file \"" +
                        Server->ConvertToUTF8(fileName) + "\" (" + "): " +
                        std::string(archive_error_string(archive)),
                        LL_ERROR);
            if (r == ARCHIVE_FATAL)
                return false;
        }
        else {
            if (!file.isdir) {
                if (!write_file_content_to_archive(archive, fileName))
                    return false;
            }
            if (file.isdir) {
                if (!traverse_dir(archive, entry,
                                  entryName, fileName, filter)) {
                    return false;
                }
            }
        }
    }
    return true;
}


static ssize_t server_write_callback(struct archive *archive,
                                     void *param,
                                     const void *buf, size_t bufLen)
{
    THREAD_ID *tid = (THREAD_ID *)param;
    if (Server->WriteRaw(*tid,
                         reinterpret_cast<const char*>(buf),
                         bufLen, false)) {
        return bufLen;
    }
    Server->Log("Error writing data to server" +
                std::string(archive_error_string(archive)), LL_ERROR);
    return 0;
}


bool create_gzip_to_output(const std::wstring &folderName,
                           const std::wstring &filter)
{
    struct archive *archive = archive_write_new();
    if (!archive) {
        Server->Log("Error creating archive: " +
                    std::string(archive_error_string(archive)), LL_ERROR);
        return false;
    }
    if (archive_write_set_format_gnutar(archive) != ARCHIVE_OK ||
        archive_write_add_filter_gzip(archive) != ARCHIVE_OK ||
        archive_write_set_bytes_in_last_block(archive, 1) != ARCHIVE_OK) {
        Server->Log("Error using tar/gzip for archive: " +
                    std::string(archive_error_string(archive)), LL_ERROR);
        archive_write_free(archive);
        return false;
    }
    THREAD_ID tid = Server->getThreadID();
    if (archive_write_open(archive, (void *)&tid,
                           NULL, /* open */
                           server_write_callback,
                           NULL /* close */
                           ) != ARCHIVE_OK) {
        Server->Log("Error opening file for archive: " +
                    std::string(archive_error_string(archive)), LL_ERROR);
        archive_write_free(archive);
        return false;
    }
    struct archive_entry *entry = archive_entry_new();
    bool rc = traverse_dir(archive, entry,
                           L"", folderName, filter);
    archive_entry_free(entry);
    if (!rc) {
        Server->Log("Error while adding files and folders to archive",
                    LL_ERROR);
    }
    if (archive_write_free(archive) != ARCHIVE_OK) {
        Server->Log("Error freeing archive: " +
                    std::string(archive_error_string(archive)),
                    LL_ERROR);
        rc = false;
    }
    return rc;
}
