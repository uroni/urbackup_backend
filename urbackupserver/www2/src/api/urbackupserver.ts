import { PBKDF2, MD5, algo } from"crypto-js";

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

enum ClientProcessActionTypes {
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

interface ClientProcessItem {
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
  processes: [ClientProcessItem];
}

interface StatusResult {
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

  status: [StatusClientItem];
}

type StartType = "incr_file" | "full_file" | "incr_image" | "full_image";

interface StartBackupResultItem {
  start_type: StartType;
  clientid: ClientIdType;
  start_ok: boolean;
}

interface StartBackupResult {
  result: [StartBackupResultItem];
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

export class SessionNotFoundError extends Error {}

export class UsernameNotFoundError extends Error {}

export class PasswordWrongError extends Error {}

export class UsernameOrPasswordWrongError extends Error {}

class UrBackupServer {
  private serverUrl: string;
  private session = "";

  constructor(serverUrl: string, session: string) {
    this.serverUrl = serverUrl;
    this.session = session;
  }

  // Generic function to fetch data from server
  fetchData = async (params: Record<string, string>, action: string) => {
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

    if(!resp.session && this.session) {
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
    const resp = await this.fetchData({"remove_client": clientid.join()}, "status")
    return resp as StatusResult;
  }

  // Mark client with id `clientId` as to be removed
  removeClient = async (clientId: ClientIdType): Promise<StatusResult> =>  {
    return await this.removeClients([clientId]);
  }

  // Unmark clients with ids `clientId` to not be removed anymore
  stopRemoveClients = async (clientid: ClientIdType[]): Promise<StatusResult> => {
    const resp = await this.fetchData({"remove_client": clientid.join(), "stop_remove_client": "true"}, "status")
    return resp as StatusResult;
  }

  // Unmark client with id `clientId` to not be removed anymore
  stopRemoveClient = async (clientId: ClientIdType): Promise<StatusResult> =>  {
    return await this.stopRemoveClients([clientId]);
  }
}

export default UrBackupServer;
