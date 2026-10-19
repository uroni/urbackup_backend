import { ClientSettingsTab, type SettingField } from "./ClientSettingsTab";
import { KBIT_PER_S, SPEED_PATTERN, speedFormat } from "./timeSpan";

const MB = 1024 * 1024;

const dataplanField = (name: string, label: string): SettingField => ({
  name,
  label,
  kind: "text",
  pattern: /^\d*\.?\d*$/,
  format: {
    ui: (v) => String(Number(v) / MB),
    api: (v) => String(Math.round(Number(v) * MB)),
  },
});

const internetFields: SettingField[] = [
  {
    name: "internet_mode_enabled",
    label: "Try to connect via Internet/active client",
    kind: "checkbox",
    mainClientOnly: true,
  },
  {
    name: "internet_authkey",
    label: "Auth key",
    kind: "text",
    mainClientOnly: true,
  },
  {
    name: "internet_image_backups",
    label: "Do image backups as Internet/active client",
    kind: "checkbox",
  },
  {
    name: "internet_full_file_backups",
    label: "Do full file backups as Internet/active client",
    kind: "checkbox",
  },
  {
    name: "internet_speed",
    label: "Max backup speed as Internet/active client (KBit/s)",
    kind: "text",
    pattern: SPEED_PATTERN,
    format: speedFormat(KBIT_PER_S),
    description: (
      <>
        "-" for no limit. Can depend on the time, e.g.{" "}
        <code>500@Mon-Fri/8-17;2000</code>
      </>
    ),
  },
  {
    name: "internet_encrypt",
    label: "Encrypted Internet/active transfer",
    kind: "checkbox",
    mainClientOnly: true,
  },
  {
    name: "internet_compress",
    label: "Compressed Internet/active transfer",
    kind: "checkbox",
    mainClientOnly: true,
  },
  {
    name: "internet_calculate_filehashes_on_client",
    label: "Calculate file-hashes on the client as Internet/active client",
    kind: "checkbox",
  },
  {
    name: "internet_parallel_file_hashing",
    label:
      "Beta: Calculate file hashes on client in parallel as Internet/active client",
    kind: "checkbox",
  },
  {
    name: "internet_connect_always",
    label:
      "Connect as Internet/active client if connected to as local/passive client",
    kind: "checkbox",
    mainClientOnly: true,
  },
  dataplanField(
    "internet_file_dataplan_limit",
    "Do not start file backups if current estimated data usage limit per month is smaller than (MB)",
  ),
  dataplanField(
    "internet_image_dataplan_limit",
    "Do not start image backups if current estimated data usage limit per month is smaller than (MB)",
  ),
];

export function InternetTab() {
  return (
    <ClientSettingsTab title="Internet/Active Client" fields={internetFields} />
  );
}
