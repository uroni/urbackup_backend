import { ClientSettingsTab, type SettingField } from "./ClientSettingsTab";

const RAW_HASHED = [
  { value: "raw", label: "Raw" },
  { value: "hashed", label: "Hashed" },
];

const INCR_FILE_MODES = [
  ...RAW_HASHED,
  { value: "blockhash", label: "Block differences - hashed" },
];

const INCR_IMAGE_STYLES = [
  { value: "to-full", label: "Based on last full image backup" },
  { value: "to-last", label: "Based on last image backup" },
];

const FULL_IMAGE_STYLES = [
  { value: "full", label: "Full image backup #1" },
  { value: "synthetic", label: "Synthetic full image backup" },
];

const select = (
  name: string,
  label: string,
  options: SettingField["options"],
): SettingField => ({ name, label, kind: "select", options });

const checkbox = (name: string, label: string): SettingField => ({
  name,
  label,
  kind: "checkbox",
});

const text = (name: string, label: string): SettingField => ({
  name,
  label,
  kind: "text",
});

const number = (name: string, label: string): SettingField => ({
  name,
  label,
  kind: "text",
  pattern: /^\d*$/,
  inputProps: { "data-field-width": "33%" },
});

const advancedFields: SettingField[] = [
  select(
    "local_full_file_transfer_mode",
    "Local/passive full file backup transfer mode",
    RAW_HASHED,
  ),
  select(
    "internet_full_file_transfer_mode",
    "Internet/active full file backup transfer mode",
    RAW_HASHED,
  ),
  select(
    "local_incr_file_transfer_mode",
    "Local/passive incremental file backup transfer mode",
    INCR_FILE_MODES,
  ),
  select(
    "internet_incr_file_transfer_mode",
    "Internet/active incremental file backup transfer mode",
    INCR_FILE_MODES,
  ),
  select(
    "local_image_transfer_mode",
    "Local/passive image backup transfer mode",
    RAW_HASHED,
  ),
  select(
    "internet_image_transfer_mode",
    "Internet/active image backup transfer mode",
    RAW_HASHED,
  ),
  select(
    "local_incr_image_style",
    "Local/passive incremental image style",
    INCR_IMAGE_STYLES,
  ),
  select(
    "internet_incr_image_style",
    "Internet/active incremental image style",
    INCR_IMAGE_STYLES,
  ),
  select(
    "local_full_image_style",
    "Local/passive full image style",
    FULL_IMAGE_STYLES,
  ),
  select(
    "internet_full_image_style",
    "Internet/active full image style",
    FULL_IMAGE_STYLES,
  ),
  checkbox(
    "end_to_end_file_backup_verification",
    "Debugging: End-to-end verification of all file backups",
  ),
  checkbox(
    "verify_using_client_hashes",
    "Debugging: Verify file backups using client side hashes",
  ),
  checkbox(
    "internet_readd_file_entries",
    "Periodically readd file entries of internet clients to database (disable only if you do not run fulls)",
  ),
  checkbox(
    "background_backups",
    "Run backups with background priority on the clients",
  ),
  checkbox(
    "create_linked_user_views",
    "Create symbolically linked views for each user on the clients after file backups",
  ),
  number(
    "max_running_jobs_per_client",
    "Maximum number of simultaneous jobs per client",
  ),
  text(
    "cbt_volumes",
    "List of volumes for which change block tracking should be used (if available)",
  ),
  text(
    "cbt_crash_persistent_volumes",
    "List of volumes for which the change block tracking should be crash persistent",
  ),
  checkbox(
    "ignore_disk_errors",
    "Do not fail backups in case of hash mismatches or read errors",
  ),
  text(
    "image_snapshot_groups",
    "Volumes to snapshot in groups during image backups",
  ),
  text(
    "file_snapshot_groups",
    "Volumes to snapshot in groups during file backups",
  ),
  {
    name: "vss_select_components",
    label: "Windows components backup configuration",
    kind: "mergeable",
  },
  text(
    "client_settings_tray_access_pw",
    "Require tray icon users to enter following text before being able to change settings",
  ),
  number(
    "download_threads",
    "Number of parallel file download threads per file backup",
  ),
  number(
    "hash_threads",
    "Number of parallel server file hash threads per file backup",
  ),
  number(
    "client_hash_threads",
    "Number of parallel client file hash threads per file backup",
  ),
  number(
    "image_compress_threads",
    "Number of threads to use for VHDZ compression (0: auto)",
  ),
  {
    ...text(
      "internet_expect_endpoint",
      "List of server IPs (proxys) from which to expect endpoint information (forwarded for) when connecting to Internet/active service (needs server restart)",
    ),
    generalOnly: true,
  },
];

export function AdvancedTab() {
  return <ClientSettingsTab title="Advanced" fields={advancedFields} />;
}
