import { ClientSettingsTab, type SettingField } from "./ClientSettingsTab";

const PERMISSIONS_LABELS = {
  allow_config_paths: "Allow client-side changing of the directories to backup",
  allow_starting_full_file_backups:
    "Allow client-side starting of full file backups",
  allow_starting_incr_file_backups:
    "Allow client-side starting of incremental file backups",
  allow_starting_full_image_backups:
    "Allow client-side starting of full image backups",
  allow_starting_incr_image_backups:
    "Allow client-side starting of incremental image backups",
  allow_log_view: "Allow client-side viewing of backup logs",
  allow_pause: "Allow client-side pausing of backups",
  allow_overwrite: "Allow client-side changing of settings",
  allow_tray_exit: "Allow clients to quit the tray icon",
  allow_file_restore: "Allow clients to start file restores",
  allow_component_config: "Allow clients to configure components to backup",
  allow_component_restore: "Allow clients to start component restores",
};

// Virtual sub clients use the permissions of their main client
const permissionsFields = Object.entries(PERMISSIONS_LABELS).map<SettingField>(
  ([name, label]) => ({ name, label, kind: "checkbox", mainClientOnly: true }),
);

export function Permissions() {
  return <ClientSettingsTab title="Permissions" fields={permissionsFields} />;
}
