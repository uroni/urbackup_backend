import type { ReactNode } from "react";
import {
  Button,
  Link,
  MessageBar,
  MessageBarActions,
  MessageBarBody,
  MessageBarIntent,
} from "@fluentui/react-components";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";

import type { StatusCheckResult, StatusResult } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";
import { serverVersionNumber } from "./serverVersion";

const runningVersion = serverVersionNumber(
  document
    .querySelector('meta[name="urbackup-version"]')
    ?.getAttribute("content"),
);

const DIR_ERROR_TEXT =
  "The directory where UrBackup will save backups is inaccessible. Please fix that by modifying this folder in 'Settings' or by giving UrBackup rights to access this directory.";

// Errors whose hint replaces the generic text above
const SPECIFIC_DIR_ERRORS = [
  "err_cannot_create_symbolic_links",
  "dos_names_created",
  "err_file_system_case_insensitive",
  "err_file_system_special_windows_files_disallowed",
  "err_long_create_failed",
  "err_btrfs_backupfolder_differs",
];

function dirErrorHint(check: StatusCheckResult): ReactNode {
  switch (check.dir_error_hint) {
    case undefined:
      return null;
    case "volume_not_accessible":
      return 'The entire drive is not accessible. If this is a network drive be aware that network drives are per user and UrBackup server runs as "Local System" user per default so it wont see your network drive. In this case you should use the UNC notation instead (\\\\servername\\share).';
    case "folder_unc_access_denied":
      return (
        <>
          UrBackup is denied access to the network share, probably because it
          does not have the correct credentials to access the server. To setup
          UrBackup server to backup to a network share please see the FAQ:{" "}
          <Link href="https://www.urbackup.org/faq.html#use_shares">
            https://www.urbackup.org/faq.html#use_shares
          </Link>
        </>
      );
    case "dos_names_created":
      return (
        <>
          MS-DOS 8.3 compatibility names are created on the backup storage. This
          can lead to problems. You can disable 8.3 name generation by running{" "}
          <code>fsutil 8dot3name set {check.dir_error_volume} 1</code> as
          administrator. UrBackup will stop showing this error after you run
          this command.
        </>
      );
    case "err_file_system_case_insensitive":
      return "UrBackup Server is running on a operating system where file systems are usually case sensitive. Work-arounds for UrBackup to work with case insensitive backup storage are disabled. The backup storage is case insensitive, however. This might cause issues.";
    case "err_file_system_special_windows_files_disallowed":
      return "UrBackup Server has trouble storing files named 'CON' to backup storage. When running on Windows UrBackup Server has work-arounds to avoid storing such files. Those work-arounds are disabled when not running on Windows, however. So if a client has files named like this, there might be issues with backups.";
    case "err_long_create_failed":
      return `UrBackup Server has trouble storing files to backup storage with the maximum file name length it was compiled with${check.max_path_str ? " " + check.max_path_str : ""}. There might be issues when backing up files/folders with long file name.`;
    case "err_btrfs_backupfolder_differs":
      return `Backup folder configured for btrfs (suid helper) at /etc/urbackup/backupfolder (${check.btrfs_backupfolder}) differs from backup folder configured for UrBackup (Settings -> General, ${check.urbackup_backupfolder}). This can lead to issues.`;
    default:
      return check.dir_error_hint;
  }
}

function Warning({
  intent = "error",
  children,
  action,
}: {
  intent?: MessageBarIntent;
  children: ReactNode;
  action?: { label: string; onClick: () => void };
}) {
  return (
    <MessageBar intent={intent} layout="multiline">
      <MessageBarBody>{children}</MessageBarBody>
      {action && (
        <MessageBarActions>
          <Button onClick={action.onClick}>{action.label}</Button>
        </MessageBarActions>
      )}
    </MessageBar>
  );
}

/**
 * Server problems the old status page showed above the client list.
 */
export function StatusWarnings({ status }: { status: StatusResult }) {
  const queryClient = useQueryClient();

  // The checks touch the backup storage, so only run them once per visit
  const { data: check } = useQuery({
    queryKey: ["status_check"],
    queryFn: urbackupServer.statusCheck,
    enabled: !!status.has_status_check,
    staleTime: Infinity,
    gcTime: 0,
  });

  const stopShow = useMutation({
    mutationFn: urbackupServer.stopShowStatusError,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["status"] });
      queryClient.invalidateQueries({ queryKey: ["status_check"] });
    },
  });

  const reset = useMutation({
    mutationFn: urbackupServer.resetStatusError,
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ["status"] }),
  });

  const stopShowVersion = useMutation({
    mutationFn: urbackupServer.stopShowNewVersion,
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ["status"] }),
  });

  const stopShowAction = (key: string | undefined, label: string) =>
    key ? { label, onClick: () => stopShow.mutate(key) } : undefined;
  const resetAction = (error: string) => ({
    label: "Reset this error",
    onClick: () => reset.mutate(error),
  });

  const newVersion = status.curr_version_str;

  return (
    <>
      {newVersion &&
        runningVersion !== undefined &&
        (status.curr_version_num ?? 0) > runningVersion && (
          <Warning
            intent="info"
            action={{
              label: "Stop showing this",
              onClick: () => stopShowVersion.mutate(newVersion),
            }}
          >
            There is a new version of UrBackup server available ({newVersion}
            ). Download it{" "}
            <Link href="https://www.urbackup.org/download.html">here</Link>.
          </Warning>
        )}
      {status.nospc_stalled && (
        <Warning action={resetAction("nospc_stalled")}>
          There is currently not enough free space in the backup folder.
          UrBackup is deleting old image and file backups to free space, within
          the limits defined by the settings. During this process the backup
          performance is decreased and backups are stalled.
        </Warning>
      )}
      {status.nospc_fatal && (
        <Warning action={resetAction("nospc_fatal")}>
          There is currently not enough free space in the backup folder.
          UrBackup tried to delete old image and file backups but is now not
          allowed to delete more. Please change the settings to store less
          backups or increase the storage amount to allow UrBackup to continue
          to perform backups.
        </Warning>
      )}
      {status.database_error && (
        <Warning action={resetAction("database_error")}>
          An error occured while accessing or checking UrBackup&apos;s internal
          database. This means this database is probably damaged or there is not
          enough free space. If this error persists, please restore the database
          (the files backup_server.db, backup_server_files.db,
          backup_server_links.db, backup_server_link_journal.db and
          backup_server_settings.db) from a backup. See log file for details.
        </Warning>
      )}
      {check?.dir_error && check.dir_error_show !== false && (
        <Warning
          action={stopShowAction(
            check.dir_error_stop_show_key,
            "Stop showing this error",
          )}
        >
          {!SPECIFIC_DIR_ERRORS.includes(check.dir_error_ext ?? "") &&
            DIR_ERROR_TEXT}
          {check.dir_error_ext && ` (${check.dir_error_ext})`}
          {check.system_err && `. ${check.system_err}`}
          {check.dir_error_hint && (
            <>
              <br />
              {dirErrorHint(check)}
            </>
          )}
          {check.detail_err_str && (
            <>
              <br />
              Detailed error info:
              <pre>{check.detail_err_str}</pre>
            </>
          )}
        </Warning>
      )}
      {check?.tmpdir_error && check.tmpdir_error_show !== false && (
        <Warning
          action={stopShowAction(
            check.tmpdir_error_stop_show_key,
            "Stop showing this error",
          )}
        >
          The directory where UrBackup will save temporary files is
          inaccessible. Please fix that by modifying this folder in
          &apos;Settings&apos; or by giving UrBackup rights to access this
          directory.
        </Warning>
      )}
      {check?.virus_error && check.virus_error_show !== false && (
        <Warning
          action={stopShowAction(
            check.virus_error_stop_show_key,
            "Stop showing this error",
          )}
        >
          On-access virus scanner active in temporary file path. This will cause
          backups to fail once your virus scanner detects a backed up file as a
          virus. Your virus scanner will also probably scan each backed up file
          multiple times causing performance problems. You should consider
          completely disabling the on-access virus scanner on the server or at
          the very least exclude UrBackup server&apos;s temporary path (
          <code>{check.virus_error_path}</code>).
        </Warning>
      )}
      {status.has_ident_error_clients &&
        status.show_has_ident_error_clients !== false && (
          <Warning
            intent="info"
            action={stopShowAction(
              status.has_ident_error_clients_stop_show_key,
              "Dismiss this hint",
            )}
          >
            This server has discovered clients which are currently not
            configured to use this server. A client only lets the first server
            it sees back it up, so this happens when a client belongs to another
            server, or this server&apos;s identity changed after a reinstall. To
            switch such a client to this server, accept the popup asking to use
            the new server on the client, or delete server_idents.txt on the
            client and restart its service.
          </Warning>
        )}
    </>
  );
}
