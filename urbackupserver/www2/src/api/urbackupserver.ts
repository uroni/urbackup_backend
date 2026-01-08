import { PBKDF2, MD5, algo } from "crypto-js";
import testoutputProgress from "./TestoutputProgress.json";
import { formatUserRights } from "../utils/formatUserRights";

interface SaltResult {
  salt: string;
  rnd: string;
  pbkdf2_rounds: number;
  error: number | undefined;
  ses: string | undefined;
}

export interface LoginResult {
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

export type ClientIdType = number;

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
  lastbackup: "-" | number;
  lastbackup_image: "-" | number;
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

export interface ClientInfo {
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

export class SessionNotFoundError extends Error { }

export class UsernameNotFoundError extends Error { }

export class PasswordWrongError extends Error { }

export class UsernameOrPasswordWrongError extends Error { }

export class BackupsAccessDeniedError extends Error { }

export class UserAlreadyExistsError extends Error { }

export class UnknownUserAddError extends Error { }

// Error parsing server response
export class ResponseParseError extends Error { }

export class UnknownUpdateRightsError extends Error { }

export class UnknownRemoveUserError extends Error { }

export class UnknownChangePasswordError extends Error { }

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

export interface UsageClientStat {
  files: number; // Number of bytes of file backup usage the client has
  images: number; // Number of bytes of image backup usage the client has
  name: string; // Name of the client
  used: number; // Combined file and image backup usage
}
export interface UsageStats {
  reset_statistics: undefined | "true"; // If string "true" the statistics can be reset
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

export const LOG_LEVELS = {
  INFO: 0,
  WARNING: 1,
  ERROR: 2,
} as const

export type LogIdType = number;

export interface LogClient {
  id: ClientIdType; // Id of the client
  name: string; // Name of the client
}

export interface LogInfo {
  name: string; // Name of the client
  id: LogIdType; // Id of the log
  time: number; // Unix timestamp of when the log was created
  errors: number; // Number of errors logged
  warnings: number; // Number of warnings logged
  image: number; // !=0 if this is a log of an image backup
  incremental: number; // !=0 if this is a log of an incremental backup
  resumed: number; // !=0 if this is a log of a resumed backup
  restore: number; // !=0 if this is a log of a restore
}

export interface LogsResp {
  clients: LogClient[]; // List of clients with logs (does not include clients that don't have logs)
  all_clients: boolean; // If true all clients are selected
  has_user: boolean; // Logged in via user. Reports can only be configured if logged in as user
  log_right_clients: LogClient[]; // List of clients the user has log rights for
  filter: string; // Filter used
  logs: LogInfo[]; // List of logs
  ll: typeof LOG_LEVELS[keyof typeof LOG_LEVELS]; // Loglevel filter
  report_mail: string[]; // List of email addresses to send the report to
  report_loglevel: "" | typeof LOG_LEVELS[keyof typeof LOG_LEVELS]; // Loglevel when to send a report
  report_sendonly: "" | SendOnly; // When to send a report
  can_report_script_edit: boolean | undefined; // If true the user can edit the report script
}

export interface LogDataRow {
  level: typeof LOG_LEVELS[keyof typeof LOG_LEVELS]; // Log level
  message: string; // Log message
  time: number; // Unix timestamp of when the log entry was created
}

export interface LogData {
  data: LogDataRow[]; // Log data
  time: number; // Unix timestamp of when the log was created
  clientname: string; // Name of the client
}

export interface LogDataResp {
  clients: LogClient[]; // List of clients with logs (does not include clients that don't have logs)
  all_clients: boolean; // If true all clients are selected
  has_user: boolean; // If true this is non-admin user
  log_right_clients: LogClient[]; // List of clients the user has log rights for
  filter: string; // Filter used
  log: LogData; // Log data
}

export enum SendOnly {
  Always = 0,
  Failed = 1,
  Succeeded = 2,
  FailedExcludingTimeout = 3
}

export interface SettingsClients {
  group: number; // Id of the group the client belongs to
  id: number; // Id of the client
  name: string; // Name of the client
  override: boolean; // At least some setting is client specific
  groupname: string
}

export interface SettingsGroups {
  id: number; // Id of group
  name: string; // Name of group
}

export interface SettingsNavitems {
  admin: boolean; // Current user is an admin user
  clients: SettingsClients[]; // List of clients
  disable_change_pw: boolean; // If true the user cannot change the password
  general: boolean; // Can change general settings
  groupmod: boolean; // Can modify which groups clients are assigned to
  groups: SettingsGroups[]; // List of groups
  internet: boolean; // Can change internet settings
  ldap: boolean; // Can change LDAP settings
  mail: boolean; // Can change mail settings
  users: boolean; // Can change user settings
}

export interface AlertScriptParam {
  default_value: string; // Default value of the parameter
  has_translation: 0 | 1; // If 1 the parameter has a translation
  label: string; // Label of the setting
  name: string; // Name of the setting
  type: string; // Type of the setting
}

export interface SettingsAlertScript {
  id: number; // Id of the alert script
  name: string; // Name of the alert script
  params: AlertScriptParam[]; // List of parameters
}

type SettingValueType = string | boolean | number;

export interface SettingState {
  value: SettingValueType; // Value of the setting
}

export interface ClientSettingState {
  value: SettingValueType; // Value of the setting
  value_group: SettingValueType; // Inherited settings value (from specific or global default group)
  value_client: SettingValueType; // Value coming from the client
  use: undefined | number;
}

export const enum UseValue {
  Group = 1,
  Server = 2,
  Client = 4
}

export function isUseValueSet(use: number, val: UseValue) {
  return (use & val) > 0;
}

export function setUseValue(use: number, val: UseValue): number {
  return use | val;
}

export function getCurrentValue(setting: ClientSettingState): SettingValueType {
  if (typeof setting.use == "undefined") {
    if (typeof setting.value != "undefined")
      return setting.value;

    if (typeof setting != "object")
      return setting;

    return "";
  }

  if (setting.use == UseValue.Group) {
    return setting.value_group;
  }
  else if (setting.use == UseValue.Server) {
    return setting.value;
  }
  else if (setting.use == UseValue.Client) {
    return setting.value_client;
  }
  else {
    return setting.value;
  }
}

export interface GeneralSettingsVals {
  can_edit_scripts: boolean; // If true the user can edit scripts
}

export interface GeneralSettings {
  sa: "general"; // Request settings sub-action
  navitems: SettingsNavitems; // Navigation items
  cowraw_available: boolean; // If true the cowraw feature is available
  settings: GeneralSettingsVals | { [key: string]: SettingState } | { [key: string]: SettingValueType }; // Settings
  saved_ok: undefined | boolean; // If true the settings were saved successfully
}

export interface ClientSettings {
  sa: "clientsettings"; // Request settings sub-action
  navitems: SettingsNavitems; // Navigation items
  settings: { [key: string]: ClientSettingState } | { [key: string]: SettingValueType };
  saved_ok: undefined | boolean; // If true the settings were saved successfully
}

export type StringBoolSetting = "true" | "false";

export interface MailSettingsVals {
  mail_servername: string; // Name of the mail server
  mail_serverport: string; // Port of the mail server
  mail_username: string; // Username for the mail server
  mail_password: string; // Password for the mail server
  mail_from: string; // Sender address for the mail server
  mail_ssl_only: StringBoolSetting; // If true SSL should be used to connect to server
  mail_check_certificate: StringBoolSetting; // If true SSL certificate should be checked
  mail_use_smtps: StringBoolSetting; // If true SMTPS should be used (instead of STARTTLS)
  mail_admin_addrs: string; // List of email addresses to send mails to (comma/semicolon separated list)
}

export interface MailSettings {
  sa: "mail"; // Request settings sub-action
  navitems: SettingsNavitems; // Navigation items
  settings: MailSettingsVals; // Settings
  mail_test?: string
}

export interface LdapSettingsVals {
  ldap_login_enabled: boolean; // If true LDAP login is enabled
  ldap_server_name: string; // Name of the LDAP server
  ldap_server_port: number; // Port of the LDAP server
  ldap_username_prefix: string; // Prefix of the LDAP username
  ldap_username_suffix: string; // Suffix of the LDAP username
  ldap_group_class_query: string; // LDAP query to get the group class
  ldap_group_key_name: string; // Key name of the group in the LDAP query
  ldap_class_key_name: string; // Key name of the class in the LDAP query
  ldap_group_rights_map: string; // Map of group rights
  ldap_class_rights_map: string; // Map of class rights
}

export interface LdapSettings {
  sa: "ldap"; // Request settings sub-action
  navitems: SettingsNavitems; // Navigation items
  settings: LdapSettingsVals; // Settings
}

export interface UserRight {
  domain: string; // Domain of the user right
  right: string; // Right of the user. E.g. "all" or a list of client ids separated by commas
}

export interface UserListItem {
  id: string; // Id of the user (integer)
  name: string; // Name of the user
  rights: UserRight[]; // List of rights the user has
}

export interface UserList {
  sa: "listusers"; // Request settings sub-action
  navitems: SettingsNavitems; // Navigation items
  users: UserListItem[]; // List of users
}

export enum AddUserResult {
  OK = 0, // User was added successfully
  USERNAME_EXISTS = 1, // Username already exists
  ERROR = 2, // An error occurred
}

function randomString() {
  const chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXTZabcdefghiklmnopqrstuvwxyz";
  const string_length = 50;
  let randomstring = '';

  const array = new Uint32Array(string_length);
  if (window.crypto && window.crypto.getRandomValues(array)) {
    for (var i = 0; i < string_length; i++) {
      randomstring += chars.charAt(array[i] % chars.length);
    }
    return randomstring;
  }

  for (var i = 0; i < string_length; i++) {
    const rnum = Math.floor(Math.random() * chars.length);
    randomstring += chars.substring(rnum, rnum + 1);
  }
  return randomstring;
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
      console.log(resp)
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
    if (typeof resp.users == "undefined")
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
    if (typeof resp.data == "undefined")
      throw new ResponseParseError("No data found in response");

    return resp.data as PieGraphData[];
  }

  // Get data for usage graph showing storage usage over time
  // scale: "d" for daily, "m" for monthly, "y" for yearly
  getUsageGraphData = async (scale: "d" | "m" | "y", clientId?: string) => {
    const params = {
      scale,
      clientid: clientId ?? ""
    }

    const resp = await this.fetchData(params, "usagegraph");
    if (typeof resp.data == "undefined")
      throw new ResponseParseError("No data found in response");

    return resp.data as UsageGraphData[];
  }

  // Start recalculation of all statistics
  recalculateStats = async () => {
    const resp = await this.fetchData({ "recalculate": "true" }, "usage");
    return resp as UsageStats;
  }

  // Get Logs
  // Use an empty filter and loglevel LOG_LEVELS.INFO to get all logs
  getLogs = async (filter: ClientIdType[], logLevel: typeof LOG_LEVELS[keyof typeof LOG_LEVELS]) => {
    const resp = await this.fetchData({ "filter": filter.join(","), "ll": String(logLevel) }, "logs");
    const ret = resp as LogsResp;
    ret.report_mail = resp.report_mail.length == 0 ? [] : resp.report_mail.split(/[;,]/);
    return ret;
  }

  // Parse log data
  parseLog = (d: string) => {
    const msgs = d.split("\n");
    const rows: LogDataRow[] = [];
    for (const msg of msgs) {
      const level = parseInt(msg.substring(0, 1)) as typeof LOG_LEVELS[keyof typeof LOG_LEVELS];
      let message: string;
      const idx = msg.indexOf("-", 2);
      let time = NaN;
      if (idx != -1) {
        time = parseInt(msg.substring(2, idx));
        message = msg.substring(idx + 1);
      } else {
        message = msg.substring(2);
      }

      rows.push({ level: level, message: message, time: time });
    }
    return rows;
  }

  // Get log data via log id
  getLog = async (logid: LogIdType) => {
    const resp = await this.fetchData({ "logid": logid.toString() }, "logs");
    const ret = resp as LogDataResp;
    ret.log.data = this.parseLog(resp.log.data);
    return ret;
  }

  // Save reporting configuration of user
  saveLogReporting = async (mails: string[], logLevel: typeof LOG_LEVELS[keyof typeof LOG_LEVELS], sendOnly: SendOnly) => {
    const resp = await this.fetchData({ "report_mail": mails.join(";"), "report_loglevel": logLevel.toString(), "report_sendonly": sendOnly.toString() }, "logs");
    const ret = resp as LogsResp;
    ret.report_mail = resp.report_mail.length == 0 ? [] : resp.report_mail.split(/[;,]/);
    return ret;
  }

  // Get general server settings
  // Returns a list of settings listed by `generalSettingsList` and `settingsList`
  // The settings of `generalSettingsList` are returned as plain [string:string] values
  // while the settings of `settingsList` are returned as [string:SettingState] values
  getGeneralSettings = async () => {
    const resp = await this.fetchData({ sa: "general" }, "settings");
    const ret = resp as GeneralSettings;
    return ret;
  }

  // Save general server settings
  saveGeneralSettings = async (
    settings: Partial<GeneralSettings> & {
      settings: GeneralSettings['settings']
    },
  ) => {
    const params: Record<string, string> = { "sa": "general_save" };
    for (const [key, value] of Object.entries(settings.settings)) {
      if (typeof value == "object")
        params[key] = (value as SettingState).value.toString();
      else
        params[key] = value.toString();
    }
    const resp = await this.fetchData(params, "settings");
    const ret = resp as GeneralSettings;
    return ret;
  }

  // Get mail server settings
  getMailSettings = async () => {
    const resp = await this.fetchData({ sa: "mail" }, "settings");
    const ret = resp as MailSettings;
    return ret;
  }

  // Save mail server settings
  // If testmailaddr is not empty, a test mail is sent to that address
  // Returns the saved settings
  saveMailSettings = async (settings: MailSettingsVals, testmailaddr: string = "") => {
    const params: Record<string, string> = { "sa": "mail_save", "testmailaddr": testmailaddr };
    for (const [key, value] of Object.entries(settings)) {
      params[key] = value.toString();
    }
    const resp = await this.fetchData(params, "settings");
    const ret = resp as MailSettings;
    return ret;
  }

  // Get LDAP settings
  // Returns a list of settings listed by `ldapSettingsList`
  getLdapSettings = async () => {
    const resp = await this.fetchData({ sa: "ldap" }, "settings");
    const ret = resp as LdapSettings;
    return ret;
  }

  // Save LDAP settings
  // If testusername is not empty, a test login is performed
  // Returns the saved settings
  saveLdapSettings = async (settings: LdapSettingsVals, testusername: string = "", testpassword: string = "") => {
    const params: Record<string, string> = { "sa": "ldap_save", "testusername": testusername, "testpassword": testpassword };
    for (const [key, value] of Object.entries(settings)) {
      params[key] = value.toString();
    }
    const resp = await this.fetchData(params, "settings");
    const ret = resp as LdapSettings;
    return ret;
  }

  getUserList = async () => {
    const resp = await this.fetchData({ sa: "listusers" }, "settings");
    return resp as UserList;
  }

  // Add a user with the given name, password and rights
  // Throws UserAlreadyExistsError if the user already exists on the server  
  createUser = async (name: string, password: string, rights: UserRight[]) => {
    const salt = randomString();
    const password_md5 = MD5(salt + password).toString();
    const params: Record<string, string> = { sa: "useradd", name: name, pwmd5: password_md5, salt: salt };
    params['rights'] = formatUserRights(rights)
    const resp = await this.fetchData(params, "settings");
    if (typeof resp.add_ok != "undefined" && resp.add_ok) {
      return;
    } else if (typeof resp.alread_exists != "undefined" && resp.alread_exists) {
      throw new UserAlreadyExistsError();
    } else {
      throw new UnknownUserAddError();
    }
  }

  // Change the rights of a user with the given userId
  // Throws UnknownUpdateRightsError if the rights could not be updated
  changeUserRights = async (userId: string, rights: string) => {
    const params: Record<string, string> = { sa: "updaterights", rights: rights, userid: userId };
    const resp = await this.fetchData(params, "settings");
    if (typeof resp.update_right != "undefined" && resp.update_right) {
      return resp as UserList;
    }

    throw new UnknownUpdateRightsError();
  }

  // Remove a user with the given userId
  // Throws UnknownRemoveUserError if the user could not be removed
  removeUser = async (userId: string) => {
    const params: Record<string, string> = { sa: "removeuser", userid: userId };
    const resp = await this.fetchData(params, "settings");
    if (typeof resp.removeuser != "undefined" && resp.removeuser) {
      return resp as UserList;
    }

    throw new UnknownRemoveUserError();
  }

  // Change the password of a user with the given userId
  // Throws UnknownChangePasswordError if the password could not be changed
  changeUserPassword = async (userId: string, password: string) => {
    // TODO: Add function to change own password
    const salt = randomString();
    const password_md5 = MD5(salt + password).toString();
    const params: Record<string, string> = { sa: "changepw", userid: userId, pwmd5: password_md5, salt: salt };
    const resp = await this.fetchData(params, "settings");
    if (typeof resp.change_ok != "undefined" && resp.change_ok) {
      return resp as UserList;
    }

    throw new UnknownChangePasswordError();
  }

  // Get list of all client and group settings
  settingsList = () => {
    return [
      "update_freq_incr",
      "update_freq_full",
      "update_freq_image_full",
      "update_freq_image_incr",
      "max_file_incr",
      "min_file_incr",
      "max_file_full",
      "min_file_full",
      "min_image_incr",
      "max_image_incr",
      "min_image_full",
      "max_image_full",
      "allow_overwrite",
      "startup_backup_delay",
      "pause_if_windows_unlocked",
      "backup_window_incr_file",
      "backup_window_full_file",
      "backup_window_incr_image",
      "backup_window_full_image",
      "computername",
      "exclude_files",
      "include_files",
      "default_dirs",
      "backup_dirs_optional",
      "allow_config_paths",
      "allow_starting_full_file_backups",
      "allow_starting_incr_file_backups",
      "allow_starting_full_image_backups",
      "allow_starting_incr_image_backups",
      "allow_pause",
      "allow_log_view",
      "allow_tray_exit",
      "allow_file_restore",
      "allow_component_restore",
      "allow_component_config",
      "image_letters",
      "internet_authkey",
      "internet_speed",
      "local_speed",
      "internet_image_backups",
      "internet_full_file_backups",
      "internet_encrypt",
      "internet_compress",
      "internet_mode_enabled",
      "silent_update",
      "client_quota",
      "virtual_clients",
      "end_to_end_file_backup_verification",
      "local_full_file_transfer_mode",
      "internet_full_file_transfer_mode",
      "local_incr_file_transfer_mode",
      "internet_incr_file_transfer_mode",
      "local_image_transfer_mode",
      "internet_image_transfer_mode",
      "internet_calculate_filehashes_on_client",
      "internet_parallel_file_hashing",
      "image_file_format",
      "internet_connect_always",
      "verify_using_client_hashes",
      "internet_readd_file_entries",
      "local_incr_image_style",
      "local_full_image_style",
      "background_backups",
      "create_linked_user_views",
      "internet_incr_image_style",
      "internet_full_image_style",
      "max_running_jobs_per_client",
      "cbt_volumes",
      "cbt_crash_persistent_volumes",
      "ignore_disk_errors",
      "image_snapshot_groups",
      "file_snapshot_groups",
      "vss_select_components",
      "internet_file_dataplan_limit",
      "internet_image_dataplan_limit",
      "update_dataplan_db",
      "alert_script",
      "alert_params",
      "archive",
      "client_settings_tray_access_pw",
      "local_encrypt",
      "local_compress",
      "download_threads",
      "hash_threads",
      "client_hash_threads",
      "image_compress_threads",
      "ransomware_canary_paths",
      "backup_dest_url",
      "backup_dest_params",
      "backup_dest_secret_params",
      "backup_unlocked_window"
    ]  as const
  }

  // Get list of general server settings
  generalSettingsList = () => {
    return [
      "backupfolder", // Folder where backups are stored : string
      "no_images", // Switch if image backups should be disabled : boolean
      "no_file_backups", // Switch if file backups should be disabled : boolean
      "autoshutdown", // Switch if autoshutdown should be enabled : boolean
      "download_client", // Switch if downloading client should be enabled : boolean
      "autoupdate_clients", // Switch if autoupdate of clients should be enabled : boolean
      "max_sim_backups", // Maximum number of simultaneous backups : integer number
      "max_active_clients", // Maximum number of active clients : integer number
      "tmpdir", // Temporary directory for backups : string
      "cleanup_window", // Time window for cleanup : string
      "backup_database", // Switch if nightly backup of database should be enabled : boolean
      "global_local_speed", // Global local speed limit: speed (MBit/s)
      "global_internet_speed", // Global internet speed limit: speed (MBit/s)
      "update_stats_cachesize", // Size of the cache for statistics : integer number (MiBytes)
      "global_soft_fs_quota", // Global soft filesystem quota: size (percentage or size in bytes)
      "server_url", // URL of the server : string/URL
      "internet_server_bind_port", // Non-default port to bind to for internet backups : integer number (port range 1-65535)
      // Advanced
      "use_tmpfiles", // Switch if tmpfiles should be used : boolean (advanced)
      "use_tmpfiles_images", // Switch if tmpfiles should be used for images : boolean (advanced)
      "use_incremental_symlinks", // Switch if incremental symlinks should be used : boolean (advanced)
      "show_server_updates", // Switch if server updates should be shown : boolean (advanced)
      // Don't add
      "internet_expect_endpoint", // Expect endpoint for internet backups : string (don't add)
    ] as const;
  }

  // Get list of mail server settings
  mailSettingsList = () => {
    return [
      "mail_servername", // Name of the mail server : string
      "mail_serverport", // Port of the mail server : integer number (port range 1-65535)
      "mail_username", // Username for the mail server : string
      "mail_password", // Password for the mail server : string (password)
      "mail_from", // Sender address for the mail server : string (with @ in it)
      "mail_ssl_only", // Switch if SSL should be used to connect to server: boolean
      "mail_check_certificate", // Switch if SSL certificate should be checked: boolean
      "mail_use_smtps", // Switch if SMTPS should be used (instead of STARTTLS): boolean
      "mail_admin_addrs" // List of email addresses to send mails to: string (comma/semicolon separated list)
    ];
  }

  // Get list of client internet server settings
  internetSettingsList = () => {
    return [
      "internet_server",
      "internet_server_proxy"
    ];
  }

  // Get list of LDAP server settings
  ldapSettingsList = () => {
    return [
      "ldap_login_enabled",
      "ldap_server_name",
      "ldap_server_port",
      "ldap_username_prefix",
      "ldap_username_suffix",
      "ldap_group_class_query",
      "ldap_group_key_name",
      "ldap_class_key_name",
      "ldap_group_rights_map",
      "ldap_class_rights_map",
      "testusername",
      "testpassword"
    ];
  }

  // Get list of settings where the group, server and client settings can be merged
  mergableSettingsList = () => {
    return [
      "virtual_clients",
      "exclude_files",
      "include_files",
      "default_dirs",
      "image_letters",
      "vss_select_components",
      "archive",
      "ransomware_canary_paths",
      "backup_dest_params",
      "backup_dest_secret_params"
    ];
  }

  // Get list of settings that can be modified on the client
  clientSettingsList = () => {
    return [
      "update_freq_incr",
      "update_freq_full",
      "update_freq_image_incr",
      "update_freq_image_full",
      "max_file_incr",
      "min_file_incr",
      "max_file_full",
      "min_file_full",
      "min_image_incr",
      "max_image_incr",
      "min_image_full",
      "max_image_full",
      "startup_backup_delay",
      "computername",
      "virtual_clients",
      "exclude_files",
      "include_files",
      "default_dirs",
      "image_letters",
      "internet_speeds",
      "local_speed",
      "internet_mode_enabled",
      "internet_full_file_backups",
      "internet_image_backups",
      "internet_compress",
      "internet_encrypt",
      "internet_connect_always",
      "vss_select_components",
      "local_compress",
      "local_encrypt"
    ] as const;
  }

  // Get client settings for client with id `clientid`
  getClientSettings = async (clientid: ClientIdType) => {
    const resp = await this.fetchData({ sa: "clientsettings", t_clientid: "" + clientid }, "settings");
    return resp as ClientSettings;
  }

  // Save client settings
  saveClientSettings = async (
    clientid: ClientIdType,
    settings: Partial<ClientSettings> & {
      settings: ClientSettings['settings']
    },
  ) => {
    const params: Record<string, string> = { sa: "clientsettings_save", t_clientid: "" + clientid };
    for (const [key, value] of Object.entries(settings.settings)) {
      if (typeof value == "object") {
        const v = (value as ClientSettingState);
        params[key] = v.value.toString();
        if (typeof v.use != "undefined") {
          params[key + ".use"] = v.use.toString();
        }
      }
      else {
        params[key] = value.toString();
      }
    }
    const resp = await this.fetchData(params, "settings");
    const ret = resp as ClientSettings;
    return ret;
  }
}

export default UrBackupServer;
