import { ClientSettingsTab, type SettingField } from "./ClientSettingsTab";

// Same check as the old web interface
const BACKUP_WINDOW_PATTERN =
  /^(([mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]-?[mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]?\s*[,]?\s*)+\/([0-9][0-9]?:?[0-9]?[0-9]?-[0-9][0-9]?:?[0-9]?[0-9]?\s*[,]?\s*)+\s*[;]?\s*)*$/i;

const backupWindowField = (
  name: string,
  label: string,
  description?: React.ReactNode,
): SettingField => ({
  name,
  label,
  kind: "text",
  pattern: BACKUP_WINDOW_PATTERN,
  description,
});

const clientFields: SettingField[] = [
  {
    name: "startup_backup_delay",
    label: "Delay after system startup",
    kind: "text",
    pattern: /^\d*$/,
    mainClientOnly: true,
    inputProps: { contentAfter: "min", "data-field-width": "33%" },
    transformer: {
      ui: (v) => v / 60,
      api: (v) => v * 60,
    },
  },
  {
    name: "pause_if_windows_unlocked",
    label: "Pause backups if Windows is unlocked",
    kind: "checkbox",
    mainClientOnly: true,
  },
  backupWindowField(
    "backup_window_incr_file",
    "Backup window for incremental file backups",
    <>
      E.g. <code>Mon-Fri/8-17;Sat,Sun/0-24</code>
    </>,
  ),
  backupWindowField(
    "backup_window_full_file",
    "Backup window for full file backups",
  ),
  backupWindowField(
    "backup_window_incr_image",
    "Backup window for incremental image backups",
  ),
  backupWindowField(
    "backup_window_full_image",
    "Backup window for full image backups",
  ),
  {
    name: "computername",
    label: "Client name",
    kind: "text",
    mainClientOnly: true,
    clientOnly: true,
  },
  {
    name: "silent_update",
    label: "Perform autoupdates silently",
    kind: "checkbox",
    mainClientOnly: true,
  },
  {
    name: "client_quota",
    label: "Soft client quota",
    kind: "text",
    description: (
      <>
        E.g. <code>20G</code> or <code>10%</code> of the backup storage
      </>
    ),
  },
  {
    name: "virtual_clients",
    label: "Additional virtual sub client names",
    kind: "mergeable",
    mainClientOnly: true,
    clientOnly: true,
    description: (
      <>
        Names separated by "|", e.g. <code>db|mail</code>
      </>
    ),
  },
  {
    name: "backup_dest_url",
    label: "Backup destination URL",
    kind: "text",
  },
  {
    name: "backup_dest_params",
    label: "Backup destination parameters",
    kind: "mergeable",
  },
  {
    name: "backup_dest_secret_params",
    label: "Backup destination secret server parameters",
    kind: "mergeable",
  },
  backupWindowField(
    "backup_unlocked_window",
    "Only run backups if unlocked in this backup window",
  ),
];

export function ClientTab() {
  return <ClientSettingsTab title="Client" fields={clientFields} />;
}
