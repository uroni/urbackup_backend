--- snapshot_helper/main.cpp.orig	2021-06-15 12:50:27 UTC
+++ snapshot_helper/main.cpp
@@ -242,12 +242,12 @@ int exec_wait(const std::string& path, std::string& st
 bool chown_dir(const std::string& dir)
 {
 	passwd* user_info = getpwnam("urbackup");
-	if(user_info)
-	{
-		int rc = chown(dir.c_str(), user_info->pw_uid, user_info->pw_gid);
-		return rc!=-1;
-	}
-	return false;
+        if(user_info)
+        {
+                int rc = chown(dir.c_str(), user_info->pw_uid, user_info->pw_gid);
+                return rc!=-1;
+        }
+        return false;
 }
 
 std::string find_btrfs_cmd()
@@ -346,7 +346,9 @@ bool create_subvolume(int mode, std::string subvolume_
 	{
 		zfs_elevate();
 		int rc=exec_wait(find_zfs_cmd(), true, "create", "-p", subvolume_folder.c_str(), NULL);
-		chown_dir(subvolume_folder);
+		std::string subvolume_path;
+		int rc = exec_wait(find_zfs_cmd(), subvolume_path, "get", "-H", "-o", "mountpoint", subvolume_folder.c_str(), NULL);
+		chown_dir(subvolume_path);
 		return rc==0;
 	}
 	return false;
@@ -389,7 +391,9 @@ bool create_snapshot(int mode, std::string snapshot_sr
 	{
 		zfs_elevate();
 		int rc=exec_wait(find_zfs_cmd(), true, "clone", (snapshot_src+"@ro").c_str(), snapshot_dst.c_str(), NULL);
-		chown_dir(snapshot_dst);
+		std::string snapshot_dst_path;
+		int rc = exec_wait(find_zfs_cmd(), snapshot_dst_path, "get", "-H", "-o", "mountpoint", snapshot_dst.c_str(), NULL);
+		chown_dir(snapshot_dst_path);
 		return rc==0;
 	}
 	return false;
