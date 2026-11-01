import { ClientSettingsTab, type SettingField } from "./ClientSettingsTab";
import { MBIT_PER_S, SPEED_PATTERN, speedFormat } from "./timeSpan";

const localFields: SettingField[] = [
  {
    name: "local_speed",
    label: "Max backup speed for local/passive transfers (MBit/s)",
    kind: "text",
    pattern: SPEED_PATTERN,
    format: speedFormat(MBIT_PER_S),
    description: (
      <>
        "-" for no limit. Can depend on the time, e.g.{" "}
        <code>10@Mon-Fri/8-17;100</code>
      </>
    ),
  },
  {
    name: "local_encrypt",
    label: "Encrypt local/passive transfers",
    kind: "checkbox",
  },
  {
    name: "local_compress",
    label: "Compress local/passive transfers",
    kind: "checkbox",
  },
];

export function LocalTab() {
  return (
    <ClientSettingsTab title="Local/Passive Client" fields={localFields} />
  );
}
