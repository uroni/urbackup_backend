#pragma once

#include <string>

class IBackupFileSystem;

bool dokany_mount(IBackupFileSystem* fs, const std::string& mount_path);
