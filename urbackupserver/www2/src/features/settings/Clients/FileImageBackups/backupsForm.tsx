import { z } from "zod/v4-mini";

import { stringToInt, VALIDATION_MESSAGES } from "../../Form/validation";
import UrBackupServer from "../../../../api/urbackupserver";
import { BaseField, Field } from "../../Form/types";
import { NewTabLink } from "../../../../components/NewTabLink";

const HOURS_IN_DAY = 24;
const SECONDS_IN_HOUR = 60 * 60;

const toSecondsFromDays = (v: number) => toSeconds(v) * HOURS_IN_DAY;
const toDaysFromSeconds = (v: number) => toHours(v) / HOURS_IN_DAY;
const toHours = (v: number) => v / SECONDS_IN_HOUR;
const toSeconds = (v: number) => v * SECONDS_IN_HOUR;

const FILEBACKUP_SETTINGS = [
  "update_freq_incr",
  "update_freq_full",
  "max_file_incr",
  "min_file_incr",
  "max_file_full",
  "min_file_full",
  "exclude_files",
  "include_files",
  "default_dirs",
  "backup_dirs_optional",
] as const satisfies ReturnType<UrBackupServer["settingsList"]>[number][];

export type FileBackupsSettingsKey = (typeof FILEBACKUP_SETTINGS)[number];

function getLabelFromName(name: string, labels: Record<string, string>) {
  if (!Object.keys(labels).includes(name)) {
    return "";
  }

  return labels[name as keyof typeof labels];
}

export const FILE_BACKUPS_LABELS: Record<FileBackupsSettingsKey, string> = {
  update_freq_incr: "Interval for incremental file backups",
  update_freq_full: "Interval for full file backups",
  max_file_incr: "Max number of incremental file backups",
  min_file_incr: "Min number of incremental file backups",
  max_file_full: "Max number of full file backups",
  min_file_full: "Min number of full file backups",
  exclude_files: "Excluded files (with wildcards)",
  include_files: "Included files (with wildcards)",
  default_dirs: "Default directories to backup",
  backup_dirs_optional: "Directories to backup are optional by default",
};

const FORM_MESSAGES = {
  update_freq_incr: VALIDATION_MESSAGES.numeric(
    FILE_BACKUPS_LABELS["update_freq_incr"],
  ),
  update_freq_full: VALIDATION_MESSAGES.numeric(
    FILE_BACKUPS_LABELS["update_freq_full"],
  ),
  max_file_incr: VALIDATION_MESSAGES.numeric(
    FILE_BACKUPS_LABELS["max_file_incr"],
  ),
  min_file_incr: VALIDATION_MESSAGES.numeric(
    FILE_BACKUPS_LABELS["min_file_incr"],
  ),
  max_file_full: VALIDATION_MESSAGES.numeric(
    FILE_BACKUPS_LABELS["max_file_full"],
  ),
  min_file_full: VALIDATION_MESSAGES.numeric(
    FILE_BACKUPS_LABELS["min_file_full"],
  ),
} satisfies Partial<Record<FileBackupsSettingsKey, string>>;

const settingsClientEnum = z.enum(["1", "2", "4"]);
const emptyOrPositive = (message: string) =>
  z.union([z.literal(""), z.coerce.number().check(z.gte(0))], message);

export const fileBackupsFormSchema = z.object({
  update_freq_incr: stringToInt(FORM_MESSAGES["update_freq_incr"]).check(
    z.int(FORM_MESSAGES["update_freq_incr"]),
  ),
  "update_freq_incr.use": settingsClientEnum,
  update_freq_full: stringToInt(FORM_MESSAGES["update_freq_full"]).check(
    z.int(FORM_MESSAGES["update_freq_full"]),
  ),
  "update_freq_full.use": settingsClientEnum,
  max_file_incr: emptyOrPositive(FORM_MESSAGES["max_file_incr"]),
  "max_file_incr.use": settingsClientEnum,
  min_file_incr: emptyOrPositive(FORM_MESSAGES["min_file_incr"]),
  "min_file_incr.use": settingsClientEnum,
  max_file_full: emptyOrPositive(FORM_MESSAGES["max_file_full"]),
  "max_file_full.use": settingsClientEnum,
  min_file_full: emptyOrPositive(FORM_MESSAGES["min_file_full"]),
  "min_file_full.use": settingsClientEnum,
  backup_dirs_optional: z.stringbool(),
  "backup_dirs_optional.use": z.enum(["1", "2"]),
});

const SETTINGS_MERGE_ENUM = z.enum(
  Array.from({ length: 8 }, (_, i) => String(i)),
);

export const excludeFilesSchema = z.object({
  exclude_files: z.optional(z.string()),
  "exclude_files.use": SETTINGS_MERGE_ENUM,
});

export const includeFilesSchema = z.object({
  include_files: z.optional(z.string()),
  "include_files.use": SETTINGS_MERGE_ENUM,
});

export const defaultDirsSchema = z.object({
  default_dirs: z.optional(z.string()),
  "default_dirs.use": SETTINGS_MERGE_ENUM,
});

const baseFileBackupsFields = [
  {
    name: "update_freq_incr",
    inputProps: {
      contentAfter: "hours",
      "data-field-width": "50%",
    },
    type: "custom",
    transformer: {
      ui: toHours,
      api: toSeconds,
    },
  },
  {
    name: "update_freq_incr.use",
    type: "custom",
  },
  {
    name: "update_freq_full",
    inputProps: {
      contentAfter: "days",
      "data-field-width": "50%",
    },
    type: "custom",
    transformer: {
      ui: toDaysFromSeconds,
      api: toSecondsFromDays,
    },
  },
  {
    name: "update_freq_full.use",
    type: "custom",
  },
  {
    name: "max_file_incr",
    type: "custom",
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "max_file_incr.use",
    type: "custom",
  },
  {
    name: "min_file_incr",
    type: "custom",
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "min_file_incr.use",
    type: "custom",
  },
  {
    name: "max_file_full",
    type: "custom",
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "max_file_full.use",
    type: "custom",
  },
  {
    name: "min_file_full",
    type: "custom",
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "min_file_full.use",
    type: "custom",
  },
  {
    name: "exclude_files",
    type: "custom",
    description: (
      <>
        You can exclude files with wildcard matching. For example if you want to
        exclude all MP3s and movie files enter something like this:{" "}
        <code>*.mp3;*.avi;*.mkv;*.mp4;*.mpg;*.mpeg</code>.{" "}
        <NewTabLink href="http://localhost:55414/help.htm#exclude_files">
          How to exclude files
        </NewTabLink>
      </>
    ),
  },
  {
    name: "exclude_files.use",
    type: "custom",
  },
  {
    name: "include_files",
    type: "custom",
    description: (
      <>
        If you do not define an include pattern all files in the backups paths
        except the excluded ones are backed up. As soon as you define an include
        pattern only files which match this pattern are backed up. Similar to
        the exclude pattern you can include files by their extension:
        <code>*.doc;*.docx</code> would only backup Word documents.{" "}
        <NewTabLink href="http://localhost:55414/help.htm#include_files">
          How to include files
        </NewTabLink>
      </>
    ),
  },
  {
    name: "include_files.use",
    type: "custom",
  },
  {
    name: "default_dirs",
    type: "custom",
    description: (
      <>
        Enter the different locations separated by a semicolon (";") e.g.
        <code>C:\Users;C:\Program Files</code>.{" "}
        <NewTabLink href="http://localhost:55414/help.htm#default_dirs">
          How to define default backup locations
        </NewTabLink>
      </>
    ),
  },
  {
    name: "default_dirs.use",
    type: "custom",
  },
  {
    name: "backup_dirs_optional",
    type: "custom",
  },
  {
    name: "backup_dirs_optional.use",
    type: "custom",
  },
] satisfies BaseField<FileBackupsSettingsKey | (string & {})>[];

export const fileBackupsFields = baseFileBackupsFields.map<
  Field<string | FileBackupsSettingsKey>
>((f) => ({
  ...f,
  label: getLabelFromName(f.name, FILE_BACKUPS_LABELS),
}));
