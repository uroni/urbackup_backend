import { z, type ZodMiniType } from "zod/v4-mini";
import { InputProps } from "@fluentui/react-components";

import { NewTabLink } from "../../../components/NewTabLink";
import { VALIDATION_MESSAGES } from "../Fields/validation";

const CLEANUP_WINDOW_REGEX =
  /^(([mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]\-?[mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]?\s*[,]?\s*)+\/([0-9][0-9]?:?[0-9]?[0-9]?\-[0-9][0-9]?:?[0-9]?[0-9]?\s*[,]?\s*)+\s*[;]?\s*)*$/i;

export const SERVER_LABELS = {
  backupfolder: "Backup storage path",
  no_images: "Do not do image backups",
  no_file_backups: "Do not do file backups",
  autoshutdown: "Automatically shut down server",
  download_client: "Download client from update server",
  autoupdate_clients: "Autoupdate clients",
  max_sim_backups: "Max simultaneous backups",
  max_active_clients: "Max recently active clients",
  tmpdir: "Nondefault temporary file directory",
  cleanup_window: "Cleanup time window",
  backup_database: "Automatically backup UrBackup database",
  global_local_speed: "Total max backup speed for local network",
  global_internet_speed:
    "Total max backup speed for Internet/active connection",
  update_stats_cachesize: "Database cache size during batch processing",
  global_soft_fs_quota: "Global soft filesystem quota",
  server_url: "Server URL for client file/backup access/browsing",
  internet_server_bind_port: "Non-default UrBackup Internet protocol TCP port",
} as const;

export const ADVANCED_SERVER_LABELS = {
  use_tmpfiles: "Temporary files as file backup buffer",
  use_tmpfiles_images: "Temporary files as image backup buffer",
  use_incremental_symlinks: "Use symlinks during incremental file backups",
  show_server_updates: "Show when a new server version is available",
} as const;

export type ServerFieldNames = keyof typeof SERVER_LABELS;
export type AdvancedServerFieldNames = keyof typeof ADVANCED_SERVER_LABELS;

function getLabelFromName(name: string, labels: Record<string, string>) {
  if (!Object.keys(labels).includes(name)) {
    return "";
  }

  return labels[name as keyof typeof labels];
}

const FORM_MESSAGES = {
  backupfolder: VALIDATION_MESSAGES.required(SERVER_LABELS["backupfolder"]),
  tmpdir: VALIDATION_MESSAGES.required(SERVER_LABELS["tmpdir"]),
  server_url:
    "The server URL should be a URL starting with http:// or https://",
  max_sim_backups: VALIDATION_MESSAGES.numeric(
    SERVER_LABELS["max_sim_backups"],
  ),
  max_active_clients: VALIDATION_MESSAGES.numeric(
    SERVER_LABELS["max_active_clients"],
  ),
  cleanup_window: VALIDATION_MESSAGES.regex(SERVER_LABELS["cleanup_window"]),
  global_local_speed: VALIDATION_MESSAGES.numeric(
    SERVER_LABELS["global_local_speed"],
  ),
  global_internet_speed: VALIDATION_MESSAGES.numeric(
    SERVER_LABELS["global_internet_speed"],
  ),
  update_stats_cachesize: VALIDATION_MESSAGES.numeric(
    SERVER_LABELS["update_stats_cachesize"],
  ),
  internet_server_bind_port:
    "The internet server bind port must have port range from 1-65535",
} as const;

const requiredStringValidation = (message?: string) =>
  z.string().check(z.minLength(1, message));

const stringToInt = (message?: string, minLength = 0) =>
  z.pipe(
    z.string().check(z.minLength(minLength, message)),
    z.transform(Number),
  );

const integerValidation = (
  message?: string,
  options?: { required?: boolean },
) =>
  stringToInt(message, options?.required ? 1 : 0).check(
    z.int(message),
    z.nonnegative(message),
  );

export const serverFormSchema: Record<ServerFieldNames, ZodMiniType> = {
  backupfolder: requiredStringValidation(FORM_MESSAGES["backupfolder"]),
  max_sim_backups: integerValidation(FORM_MESSAGES["max_sim_backups"], {
    required: true,
  }),
  max_active_clients: integerValidation(
    FORM_MESSAGES["max_active_clients"],
    {},
  ),
  tmpdir: z.string(FORM_MESSAGES["tmpdir"]),
  cleanup_window: z
    .string()
    .check(z.regex(CLEANUP_WINDOW_REGEX, FORM_MESSAGES["cleanup_window"])),
  global_local_speed: integerValidation(FORM_MESSAGES["global_local_speed"]),
  global_internet_speed: integerValidation(
    FORM_MESSAGES["global_internet_speed"],
  ),
  update_stats_cachesize: integerValidation(
    FORM_MESSAGES["update_stats_cachesize"],
  ),
  global_soft_fs_quota: z.string(),
  server_url: z.union(
    [
      z.literal(""),
      z.url({
        protocol: /^https?$/,
        hostname: z.regexes.domain,
      }),
    ],
    FORM_MESSAGES["server_url"],
  ),
  internet_server_bind_port: z.union(
    [z.literal(""), z.coerce.number().check(z.minimum(1), z.maximum(65535))],
    FORM_MESSAGES["internet_server_bind_port"],
  ),
  no_images: z.boolean(),
  no_file_backups: z.boolean(),
  autoshutdown: z.boolean(),
  download_client: z.boolean(),
  autoupdate_clients: z.boolean(),
  backup_database: z.boolean(),
};

export const advancedServerFormSchema: Record<
  AdvancedServerFieldNames,
  ZodMiniType
> = {
  use_tmpfiles: z.boolean(),
  use_tmpfiles_images: z.boolean(),
  use_incremental_symlinks: z.boolean(),
  show_server_updates: z.boolean(),
};

interface BaseField<Names> {
  name: Names;
  type: "checkbox" | "number" | "text";
  validation?: (options?: { label?: string }) => z.ZodMiniType;
  description?: React.ReactNode;
  hint?: string;
  inputProps?: InputProps & {
    "data-field-width": string;
  };
  transformer?: {
    ui: (v: number) => number | string;
    api: (v: number) => number | string;
  };
}

export interface Field<Name> extends BaseField<Name> {
  label: string;
}

const DATA_RATE_UNIT_LOCAL = "Mbit/s";
const DATA_RATE_UNIT_INTERNET = "kbit/s";

const baseServerFields: BaseField<ServerFieldNames>[] = [
  {
    name: "backupfolder",
    type: "text",
  },
  {
    name: "no_images",
    type: "checkbox",
  },
  {
    name: "no_file_backups",
    type: "checkbox",
  },
  {
    name: "autoshutdown",
    type: "checkbox",
  },
  {
    name: "download_client",
    type: "checkbox",
  },
  {
    name: "autoupdate_clients",
    type: "checkbox",
  },
  {
    name: "max_sim_backups",
    type: "number",
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "max_active_clients",
    type: "number",
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "tmpdir",
    type: "text",
  },
  {
    name: "cleanup_window",
    type: "text",
    description: (
      <>
        Pick a time when old backups are cleaned. e.g. 1-7/3-4 means clean-up
        will start on each day (1-Monday - 7-Sunday) between 3 am and 4 am.{" "}
        <NewTabLink href="http://127.0.0.1:55414/help.htm#cleanup_window">
          How to define a clean-up window
        </NewTabLink>
      </>
    ),
  },
  {
    name: "backup_database",
    type: "checkbox",
  },
  {
    name: "global_local_speed",
    type: "number",
    inputProps: {
      contentAfter: DATA_RATE_UNIT_LOCAL,
      "data-field-width": "50%",
      placeholder: "-",
    },
    transformer: {
      ui: (v: number) => (v === 0 ? "-" : bytesPStoBitsPS(v, "M")),
      api: (v: number) => bitsPStoBytesPS(v, "M"),
    },
  },
  {
    name: "global_internet_speed",
    type: "number",
    inputProps: {
      contentAfter: DATA_RATE_UNIT_INTERNET,
      "data-field-width": "50%",
      placeholder: "-",
    },
    transformer: {
      ui: (v: number) => (v === 0 ? "-" : bytesPStoBitsPS(v, "k")),
      api: (v: number) => bitsPStoBytesPS(v, "k"),
    },
  },
  {
    name: "update_stats_cachesize",
    type: "number",
    inputProps: {
      contentAfter: "MB",
      "data-field-width": "50%",
    },
    transformer: {
      ui: (v: number) => v / 1024,
      api: (v: number) => v * 1024,
    },
  },
  {
    name: "global_soft_fs_quota",
    type: "text",
    description: (
      <>
        Set how much of the max storage space to use before old backups are
        cleaned up. e.g. "95%", "50G".{" "}
        <NewTabLink href="http://127.0.0.1:55414/help.htm#global_soft_fs_quota">
          Global soft filesystem quota
        </NewTabLink>
      </>
    ),
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "server_url",
    type: "text",
  },
  {
    name: "internet_server_bind_port",
    type: "number",
    inputProps: {
      "data-field-width": "33%",
    },
    hint: "Port range 1-65535",
  },
];

const baseAdvancedServerFields: BaseField<AdvancedServerFieldNames>[] = [
  {
    name: "use_tmpfiles",
    type: "checkbox",
  },
  {
    name: "use_tmpfiles_images",
    type: "checkbox",
  },
  {
    name: "use_incremental_symlinks",
    type: "checkbox",
  },
  {
    name: "show_server_updates",
    type: "checkbox",
  },
];

export const serverFields = baseServerFields.map<Field<ServerFieldNames>>(
  (f) => ({
    ...f,
    label: getLabelFromName(f.name, SERVER_LABELS),
  }),
);

export const advancedServerFields = baseAdvancedServerFields.map<
  Field<AdvancedServerFieldNames>
>((f) => ({
  ...f,
  label: getLabelFromName(f.name, ADVANCED_SERVER_LABELS),
}));

// TODO: "Megabit per second (Mbit/s)" is in powers of 1000 instead of 1024, like in original code.
// Is it okay to fix the conversion or was the UI meant to use `Mibit/s` instead?
const RATE_POWER = 1024;

function bitsPStoBytesPS(value: number, suffix?: "M" | "k") {
  const newValue = value * RATE_POWER;

  if (suffix === "k") {
    return bitsPStoBytesPS(newValue);
  }

  if (suffix === "M") {
    return bitsPStoBytesPS(newValue, "k");
  }

  return value / 8;
}

function bytesPStoBitsPS(value: number, suffix?: "M" | "k") {
  const newValue = value / RATE_POWER;

  if (suffix === "k") {
    return bytesPStoBitsPS(newValue);
  }

  if (suffix === "M") {
    return bytesPStoBitsPS(newValue, "k");
  }

  return value * 8;
}
