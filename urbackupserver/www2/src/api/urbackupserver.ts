import { PBKDF2, MD5, algo } from "crypto-js";
import testoutputProgress from "./TestoutputProgress.json";

interface SaltResult {
  salt: string;
  rnd: string;
  pbkdf2_rounds: number;
  error: number | undefined;
  ses: string | undefined;
}

interface LoginResult {
  upgrading_database: boolean | undefined;
  curr_db_version: number | undefined;
  target_db_version: number | undefined;
  creating_filescache: boolean | undefined;
  processed_file_entries: number | undefined;
  percent_finished: number | undefined;
  ldap_enabled: boolean | undefined;
  success: boolean | undefined;
  session: string | undefined;
  admin_only: string | undefined;
  api_version: number;
  lang: string | undefined;
  error: number | undefined;

  // Permissions to bits of UI
  status: string | undefined;
  graph: string | undefined;
  progress: string | undefined;
  browse_backups: string | undefined;
  settings: string | undefined;
  logs: string | undefined;
}

type ClientIdType = number;

export enum ClientProcessActionTypes {
  NONE = 0,
  INCR_FILE = 1,
  FULL_FILE = 2,
  INCR_IMAGE = 3,
  FULL_IMAGE = 4,
  RESUME_INCR_FILE = 5,
  RESUME_FULL_FILE = 6,
  CDP_SYNC = 7,
  RESTORE_FILE = 8,
  RESTORE_IMAGE = 9,
  UPDATE = 10,
  CHECK_INTEGRITY = 11,
  BACKUP_DATABASE = 12,
  RECALCULATE_STATISTICS = 13,
  NIGHTLY_CLEANUP = 14,
  EMERGENCY_CLEANUP = 15,
  STORAGE_MIGRATION = 16,
  STARTUP_RECOVERY = 17,
}

enum ClientSpecificStatus {
  IDENT_ERROR = 11,
  TOO_MANY_CLIENTS = 12,
  AUTHENTICATION_ERROR = 13,
  UID_CHANGED = 14,
  AUTHENTICATING = 15,
  GETTING_SETTINGS = 16,
  STARTING_UP = 17,
}

export enum BackupType {
  INCR_FILE,
  FULL_FILE,
  INCR_IMAGE,
  FULL_IMAGE,
}

function backupTypeToStr(backupType: BackupType): string {
  switch (backupType) {
    case BackupType.INCR_FILE:
      return "incr_file";
    case BackupType.FULL_FILE:
      return "full_file";
    case BackupType.INCR_IMAGE:
      return "incr_image";
    case BackupType.FULL_IMAGE:
      return "full_image";
    default:
      return "undefined backup type";
  }
}

export interface ClientProcessItem {
  action: ClientProcessActionTypes;
  pcdone: number;
}

export interface StatusClientItem {
  id: ClientIdType;
  name: string;
  lastbackup: number;
  lastbackup_image: number;
  delete_pending: string;
  uid: string;
  last_filebackup_issues: number;
  no_backup_paths: boolean | undefined;
  groupname: string;
  file_ok: boolean;
  image_ok: boolean;
  file_disabled: boolean | undefined;
  image_disabled: boolean | undefined;
  image_not_supported: boolean | undefined;
  online: boolean;
  ip: string;
  client_version_string: string;
  os_version_string: string;
  os_simple: string;
  status: ClientSpecificStatus | ClientProcessActionTypes;
  lastseen: number;
  processes: ClientProcessItem[];
}

export interface StatusResult {
  has_status_check: boolean | undefined;
  nospc_stalled: boolean | undefined;
  nospc_fatal: boolean | undefined;
  database_error: boolean | undefined;

  allow_modify_clients: boolean | undefined;
  remove_client: boolean | undefined;
  allow_add_client: boolean | undefined;

  no_images: boolean;
  no_file_backups: boolean;

  admin: boolean | undefined;

  server_identity: string;
  server_pubkey: string;

  status: StatusClientItem[];
}

export type StartType = "incr_file" | "full_file" | "incr_image" | "full_image";

export interface StartBackupResultItem {
  start_type: StartType;
  clientid: ClientIdType;
  start_ok: boolean;
}

export interface StartBackupResult {
  result: StartBackupResultItem[];
}

export interface BackupsErr {
  err: string | "access_denied" | undefined;
}

export interface BackupsClient {
  id: number; // client id
  lastbackup: number; // unix timestamp
  name: string; // name of client
}

export interface BackupsClients {
  clients: BackupsClient[];
}

export interface Backup {
  id: number; // Backup id
  size_bytes: number; // Size of backup in bytes
  incremental: number; // !=0 if this is a incremental backup
  archive_timeout: number | undefined; // if not undefined or zero, unix timestamp of when the backup will be un-archived
  can_archive: boolean; // Backup can be archived
  clientid: number; // Id of the client that had the backup
  backuptime: number; // Unix timestamp of when the backup was made
  archived: number; // !=0 if this backup is archived
  disable_delete: true | undefined; // If true backup cannot be deleted
  delete_pending: true | undefined; // If true the backup is marked for deletion
}

export interface Backups {
  delete_now_err: undefined | "delete_file_backup_failed" | string; // Error message if delete now failed
  backups: Backup[];
  backup_images: undefined | Backup[];
  can_archive: boolean; // If true backups can be archived
  can_delete: boolean; // If true backups can be deleted
  clientname: string; // Name of the client
  clientid: number; // Id of the client
}

export interface File {
  name: string; // Name of the file
  dir: boolean; // If true this is a directory
  mod: number; // Unix timestamp of last modification
  creat: number; // Unix timestamp of creation
  access: number; // Unix timestamp of last access
  size: undefined | number; // Size of the file in bytes (undefined if it is a directory)
  shahash: undefined | string; // SHA hash of the file (undefined if it is a directory or if the hash is not available)
}

export interface ImageBackupInfo {
  id: number; // Backup id
  backuptime: number; // Unix timestamp of when the backup was made
  incremental: number; // !=0 if this is a incremental backup
  size_bytes: number; // Size of backup in bytes
  letter: string; // Drive letter of the image backup
  archived: number; // !=0 if this backup is archived
  archive_timeout: undefined | number; // if not undefined or zero, unix timestamp of when the backup will be un-archived
  part_table: undefined | "MBR" | "GPT"; // Partition table type
  disk_number: undefined | number; // Disk number
  partition_number: undefined | number; // Partition number
  volume_name: undefined | string; // Volume name
  fs_type: undefined | string; // Filesystem type
  serial_number: undefined | string; // Serial number of backed up volume
  linux_image_restore: undefined | true; // If true the image backup is an image of a Linux system
  volume_size: undefined | number; // Size of the volume in bytes
}

export interface Files {
  single_item: boolean; // If true there is only one item in the list
  is_file: undefined | boolean; // If single item, if it is a file
  backupid: number; // Backup id
  backuptime: number; // Unix timestamp of when the backup was made
  clientname: undefined | string; // Name of the client that had the backup
  clientid: undefined | number; // Id of the client that had the backup
  path: undefined | string; // Path of the files
  can_restore: true | undefined; // If true the files can be restored
  server_confirms_restore: true | undefined; // If true the server confirms the restore
  image_backup_info: undefined | ImageBackupInfo; // If this is an image backup, the image backup info
  mount_in_progress: true | undefined; // If true the image backup is currently being mounted
  no_files: true | undefined; // If true there are no files in the backup
  can_mount: true | undefined; // If true the image backup can be mounted
  os_mount: true | undefined; // If true the image backup can be mounted non-sandboxed
  mount_failed: true | undefined; // If true the image backup mount failed
  mount_errmsg: undefined | string; // Error message if mount failed
  files: File[]; // Files in the backup at path
}

export interface ClientInfo
{
  id: number;
  name: string;
}

function calcPwHash(
  salt: string,
  rnd: string,
  password: string,
  rounds: number,
) {
  const pwmd5Bin = MD5(salt + password);
  let pwmd5 = pwmd5Bin.toString();
  if (rounds > 0) {
    pwmd5 = PBKDF2(pwmd5Bin, salt, {
      iterations: rounds,
      hasher: algo.SHA256,
      keySize: 256 / 32,
    }).toString();
  }

  return MD5(rnd + pwmd5).toString();
}

export type OsType = "windows" | "linux" | "mac";

export class SessionNotFoundError extends Error {}

export class UsernameNotFoundError extends Error {}

export class PasswordWrongError extends Error {}

export class UsernameOrPasswordWrongError extends Error {}

export class BackupsAccessDeniedError extends Error {}

// Error parsing server response
export class ResponseParseError extends Error {}

export class BackupsAccessError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "BackupsAccessError";
  }
}

function handleBackupsErr(resp: BackupsErr) {
  if (resp.err == undefined) return;
  else if (resp.err == "access_denied") throw new BackupsAccessDeniedError();
  else throw new BackupsAccessError(resp.err);
}

function prepareBackups(backups: Backups) {
  if (backups.backup_images) {
    for (const backup of backups.backup_images) {
      backup.id *= -1;
    }
  }
  return backups;
}
export interface ProcessItem {
  action: ClientProcessActionTypes;
  pcdone: number; // Percentage (0-100) or <0 if currently e.g. "Indexing"
  eta_ms: number; // Number of milliseconds estimated to be left for the process to finish
  speed_bpms: number; // Backup speed in bytes per millisecond
  total_bytes: number; // Total number of bytes to be backed up
  done_bytes: number; // Number of bytes already backed up
  can_show_backup_log: boolean; // Can show log (so show the "Show log" button)
  can_stop_backup: boolean; // Can stop this process, so show the "stop" button
  clientid: number; // Id of client this process runs for
  detail_pc: number; // Can be a more detailed percentage
  details: string; // Details about what is currently happening e.g. drive letter being backed up
  id: number; // Id of the process
  logid: number; // Id of where the logs go
  name: string; // Name of the client the process belongs to
  past_speed_bpms: number[]; // Array of the speed history (per second)
  paused: boolean; // Is this process currently paused
  queue: number; // Number of queued files/objects
}

export interface ActivityItem {
  restore: number; // !=0 if this is a restore
  image: number; // !=0 if this is a image backup
  resumed: number; // !=0 if this is a resumed backup
  incremental: number; // !=0 if this is a incremental backup
  size_bytes: number; // Size of backup in bytes
  duration: number; // Backup duration in seconds
  backuptime: number; // Unix timestamp of backup
  clientid: number; // Id of the client that had the activity
  del: boolean; // This was a deletion activitiy or not
  details: string; // Details about the activitiy. E.g. drive letter that was backed up
  id: number; // Id of the activity/backup
  name: string; // Name of the client that had the activity
}

export interface ProgressResult {
  progress: ProcessItem[];
  lastacts: ActivityItem[] | undefined;
}

export interface UsageClientStat
{
  files: number; // Number of bytes of file backup usage the client has
  images: number; // Number of bytes of image backup usage the client has
  name: string; // Name of the client
  used: number; // Combined file and image backup usage
}
export interface UsageStats
{
  reset_statistics: undefined|"true"; // If string "true" the statistics can be reset
  usage: UsageClientStat[]; // Usage stats for each client
}

export interface PieGraphData {
  data: number; // Number of bytes used for backups of this client
  label: string; // Name of the client
}

export interface UsageGraphData {
  data: number; // Number of GiB used for backups
  xlabel: string; // ISO Date of the data (YYYY-MM-DD)
}

class UrBackupServer {
  private serverUrl: string;
  private session = "";

  constructor(serverUrl: string, session: string) {
    this.serverUrl = serverUrl;
    this.session = session;
  }

  // Generic function to fetch data from server
  fetchData = async (params: Record<string, string>, action: string) => {
    const useTestoutput = true;

    if (useTestoutput && action == "progress") return testoutputProgress;

    const searchParams = new URLSearchParams();

    for (const [key, value] of Object.entries(params)) {
      searchParams.append(key, value);
    }

    if (this.session) {
      searchParams.append("ses", this.session);
    }

    const resp = await fetch(this.serverUrl + "?a=" + action, {
      method: "post",
      cache: "no-cache",
      body: searchParams,
    });

    const ret = await resp.json();

    if (typeof ret.error != "undefined" && ret.error === 1) {
      throw new SessionNotFoundError();
    }

    return ret;
  };

  fetchSalt = async (username: string) => {
    return (await this.fetchData({ username: username }, "salt")) as SaltResult;
  };

  anonymousLogin = async () => {
    this.session = "";
    const resp = (await this.fetchData({}, "login")) as LoginResult;

    if (typeof resp.session != "undefined") {
      this.session = resp.session;
    }

    return resp;
  };

  // Login to server with username and password
  login = async (
    username: string,
    password: string,
    ldapLogin: boolean = false,
  ) => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    let resp: any;
    if (ldapLogin) {
      resp = await this.fetchData(
        { username: username, password: password, plainpw: "1" },
        "login",
      );
      if (typeof resp.error != "undefined" && resp.error == 2) {
        throw new UsernameOrPasswordWrongError();
      }

      if (resp.session) {
        this.session = resp.session;
      }
    } else {
      const saltResp = await this.fetchSalt(username);

      if (typeof saltResp.error != "undefined") {
        if (saltResp.error == 0) {
          throw new UsernameNotFoundError();
        } else if (saltResp.error == 2) {
          throw new PasswordWrongError();
        }
      }

      if (saltResp.ses) {
        this.session = saltResp.ses;
      }

      const pwmd5 = calcPwHash(
        saltResp.salt,
        saltResp.rnd,
        password,
        saltResp.pbkdf2_rounds,
      );

      resp = (await this.fetchData(
        { username: username, password: pwmd5 },
        "login",
      )) as LoginResult;

      const loginResult = resp as LoginResult;

      if (!loginResult.success && typeof loginResult.error != "undefined") {
        if (loginResult.error == 2) {
          throw new UsernameOrPasswordWrongError();
        }
      }
    }

    if (!resp.session && this.session) {
      resp.session = this.session;
    }

    return resp as LoginResult;
  };

  // Return data for displaying status page
  status = async () => {
    const resp = await this.fetchData({}, "status");
    return resp as StatusResult;
  };

  // Start a backup of type `backupType` for client with id `clientId`
  startBackup = async (clientId: ClientIdType[], backupType: BackupType) => {
    const resp = await this.fetchData(
      {
        start_client: clientId.join(),
        start_type: backupTypeToStr(backupType),
      },
      "start_backup",
    );
    return resp as StartBackupResult;
  };

  // Mark clients with ids `clientId` as to be removed
  removeClients = async (clientid: ClientIdType[]): Promise<StatusResult> => {
    const resp = await this.fetchData(
      { remove_client: clientid.join() },
      "status",
    );
    return resp as StatusResult;
  };

  // Mark client with id `clientId` as to be removed
  removeClient = async (clientId: ClientIdType): Promise<StatusResult> => {
    return await this.removeClients([clientId]);
  };

  // Unmark clients with ids `clientId` to not be removed anymore
  stopRemoveClients = async (
    clientid: ClientIdType[],
  ): Promise<StatusResult> => {
    const resp = await this.fetchData(
      { remove_client: clientid.join(), stop_remove_client: "true" },
      "status",
    );
    return resp as StatusResult;
  };

  // Unmark client with id `clientId` to not be removed anymore
  stopRemoveClient = async (clientId: ClientIdType): Promise<StatusResult> => {
    return await this.stopRemoveClients([clientId]);
  };

  // Get base URL of current site (in browser)
  getSiteURL = (): string => {
    let siteUrl = location.protocol + "//" + location.host + location.pathname;

    if (siteUrl.endsWith("index.htm")) {
      siteUrl = siteUrl.slice(0, -9);
    } else if (siteUrl.endsWith("index.html")) {
      siteUrl = siteUrl.slice(0, -10);
    }

    if (siteUrl.substring(siteUrl.length - 1) != "/") {
      siteUrl += "/";
    }

    return siteUrl;
  };

  // Get a download link for a client
  downloadClientURL = (
    clientid: ClientIdType,
    authkey: string | undefined,
    os: OsType,
  ) => {
    const params = new URLSearchParams();
    params.append("a", "download_client");
    params.append("ses", this.session);
    params.append("clientid", "" + clientid);
    params.append("os", os);

    if (authkey !== undefined) {
      params.append("authkey", authkey);
    }
    return this.getSiteURL() + this.serverUrl + "?" + params.toString();
  };

  // Returns current running processes and last activities if withLastActivities is true
  progress = async (withLastActivities: boolean) => {
    const resp = await this.fetchData(
      { with_lastacts: withLastActivities ? "1" : "0" },
      "progress",
    );
    return resp as ProgressResult;
  };

  // Stops a certain process identified by client and process id
  // Returns last activities if withLastActivities is true
  stopProcess = async (
    clientid: number,
    processId: number,
    withLastActivities: boolean,
  ) => {
    const resp = await this.fetchData(
      {
        with_lastacts: withLastActivities ? "1" : "0",
        stop_clientid: "" + clientid,
        stop_id: "" + processId,
      },
      "progress",
    );
    return resp as ProgressResult;
  };

  // Get backup clients
  getBackupsClients = async () => {
    const resp = await this.fetchData({}, "backups");
    handleBackupsErr(resp as BackupsErr);
    return resp as BackupsClients;
  };

  // Get backups of a client
  getBackups = async (clientid: number) => {
    const resp = await this.fetchData(
      { sa: "backups", clientid: "" + clientid },
      "backups",
    );
    handleBackupsErr(resp as BackupsErr);
    return prepareBackups(resp as Backups);
  };

  // Get files in a backup
  // Returns a list of files in the backup
  // If mount is true, the image backup is mounted automatically
  getFiles = async (
    clientid: number,
    backupid: number,
    path: string,
    mount?: boolean,
  ) => {
    const resp = await this.fetchData(
      {
        sa: "files",
        clientid: "" + clientid,
        backupid: "" + backupid,
        path: path,
        mount: mount ? "1" : "0",
      },
      "backups",
    );
    handleBackupsErr(resp as BackupsErr);
    return resp as Files;
  };

  // Archive a backup
  archiveBackup = async (clientid: number, backupid: number) => {
    const resp = await this.fetchData(
      { sa: "backups", clientid: "" + clientid, archive: "" + backupid },
      "backups",
    );
    handleBackupsErr(resp as BackupsErr);
    return prepareBackups(resp as Backups);
  };

  // Unarchive a backup
  unarchiveBackup = async (clientid: number, backupid: number) => {
    const resp = await this.fetchData(
      { sa: "backups", clientid: "" + clientid, unarchive: "" + backupid },
      "backups",
    );
    handleBackupsErr(resp as BackupsErr);
    return prepareBackups(resp as Backups);
  };

  // Delete a backup
  deleteBackup = async (clientid: number, backupid: number) => {
    const resp = await this.fetchData(
      { sa: "backups", clientid: "" + clientid, delete: "" + backupid },
      "backups",
    );
    handleBackupsErr(resp as BackupsErr);
    return prepareBackups(resp as Backups);
  };

  // Stop deleting a backup
  stopDeleteBackup = async (clientid: number, backupid: number) => {
    const resp = await this.fetchData(
      { sa: "backups", clientid: "" + clientid, stop_delete: "" + backupid },
      "backups",
    );
    handleBackupsErr(resp as BackupsErr);
    return prepareBackups(resp as Backups);
  };

  // Delete a backup now
  deleteBackupNow = async (clientid: number, backupid: number) => {
    const resp = await this.fetchData(
      { sa: "backups", clientid: "" + clientid, delete_now: "" + backupid },
      "backups",
    );
    handleBackupsErr(resp as BackupsErr);
    return prepareBackups(resp as Backups);
  };

  // Get a download link for a file in a backup
  downloadFileURL = (clientid: number, backupid: number, path: string) => {
    const params = new URLSearchParams();
    params.append("a", "backups");
    params.append("sa", "filesdl");
    params.append("ses", this.session);
    params.append("clientid", "" + clientid);
    params.append("backupid", "" + backupid);
    params.append("path", path);
    return this.getSiteURL() + this.serverUrl + "?" + params.toString();
  };

  // Get a download link for a zip of a directory in a backup
  downloadZipURL = (clientid: number, backupid: number, path: string) => {
    const params = new URLSearchParams();
    params.append("a", "backups");
    params.append("sa", "zipdl");
    params.append("ses", this.session);
    params.append("clientid", "" + clientid);
    params.append("backupid", "" + backupid);
    params.append("path", path);
    return this.getSiteURL() + this.serverUrl + "?" + params.toString();
  };

  // Gets the clients that are on the server
  getClients = async () => {
    const resp = await this.fetchData({}, "users");
    if(typeof resp.users == "undefined")
      throw new ResponseParseError("No users found in response");

    return resp.users as ClientInfo[];
  }

  // Get information about storage usage by client
  getUsageStats = async () => {
    const resp = await this.fetchData({}, "usage");
    return resp as UsageStats;
  }

  // Get data for pie graph showing storage usage by client
  getPiegraphData = async () => {
    const resp = await this.fetchData({}, "piegraph");
    if(typeof resp.data == "undefined")
      throw new ResponseParseError("No data found in response");

    return resp.data as PieGraphData[];
  }

  // Get data for usage graph showing storage usage over time
  // scale: "d" for daily, "m" for monthly, "y" for yearly
  getUsageGraphData = async (scale: "d"|"m"|"y", clientId?: string) => {
    const params = {
      scale, 
      clientid: clientId ?? ""
    }

    const resp = await this.fetchData(params, "usagegraph");
    if(typeof resp.data == "undefined")
      throw new ResponseParseError("No data found in response");

    return resp.data as UsageGraphData[];
  }

  // Start recalculation of all statistics
  recalculateStats = async () => {
    const resp = await this.fetchData({"recalculate": "true"}, "usage");
    return resp as UsageStats;
  }
}

export default UrBackupServer;
