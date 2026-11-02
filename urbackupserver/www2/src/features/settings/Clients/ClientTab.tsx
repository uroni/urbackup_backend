import { useState } from "react";
import { Button, Field as FUIField, Select } from "@fluentui/react-components";
import { z, type ZodMiniType } from "zod/v4-mini";

import type {
  ClientSettings,
  ClientSettingState,
} from "../../../api/urbackupserver";
import textStyles from "../Form/TextField.module.css";
import { VALIDATION_MESSAGES } from "../Form/validation";
import { useSettings } from "../useSettings";
import {
  ClientSettingsTab,
  SettingInput,
  SOURCES,
  type SettingField,
} from "./ClientSettingsTab";
import { getSettingState } from "./FileImageBackups/FileImageBackups";
import {
  SETTINGS_SOURCE,
  USE_VALUES,
  VALUE_TO_USE,
} from "./FileImageBackups/utils";
import selectStyles from "./SelectDropdown.module.css";

/**
 * Group of the client, only for users that may change it
 */
function GroupMembership({
  settings,
}: {
  settings: ClientSettings["settings"];
}) {
  const { navitems } = useSettings();

  const [group, setGroup] = useState(
    String((settings as Record<string, unknown>).memberof ?? 0),
  );

  if (!navitems.groupmod) {
    return null;
  }

  return (
    <FUIField
      label="Group"
      orientation="horizontal"
      className={`${textStyles.field} ${selectStyles.field}`}
    >
      <div className={selectStyles.row}>
        <input type="hidden" name="memberof" value={group} />
        <Select
          className={selectStyles.select}
          value={group}
          onChange={(_, d) => setGroup(d.value)}
        >
          {navitems.groups.map((g) => (
            <option key={g.id} value={String(g.id)}>
              {g.name || "Default"}
            </option>
          ))}
        </Select>
      </div>
    </FUIField>
  );
}

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

const [incrFileWindow, ...otherWindows] = [
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
];

const combinedWindow = { ...incrFileWindow, label: "Backup window" };

const HERE_VALUE = VALUE_TO_USE[USE_VALUES[SETTINGS_SOURCE.HERE]];

/**
 * Like the old web interface one backup window for all backup types while
 * they are the same. The combined input is the incremental file backup one
 */
function BackupWindows({
  settings,
  validationMessages = {},
}: {
  settings: ClientSettings["settings"];
  validationMessages?: Record<string, string>;
}) {
  const [states, setStates] = useState(() =>
    [incrFileWindow, ...otherWindows].map((f) =>
      getSettingState<ClientSettingState>(settings, f.name),
    ),
  );

  const [separate, setSeparate] = useState(() =>
    states.some((s) => JSON.stringify(s) !== JSON.stringify(states[0])),
  );

  if (separate) {
    return [incrFileWindow, ...otherWindows].map((f, i) => (
      <SettingInput
        key={f.name}
        field={f}
        settings={
          { ...settings, [f.name]: states[i] } as ClientSettings["settings"]
        }
        validationMessage={validationMessages[f.name]}
      />
    ));
  }

  return (
    <div>
      <SettingInput
        field={combinedWindow}
        settings={settings}
        validationMessage={validationMessages[incrFileWindow.name]}
      />
      <Button
        appearance="subtle"
        size="small"
        onClick={(e) => {
          // Start the separate inputs with the unsaved combined value
          // (the hidden inputs, the dropdown has the ".use" name as its id)
          const hidden = (name: string) =>
            e.currentTarget.form!.querySelector<HTMLInputElement>(
              `input[type=hidden][name="${name}"]`,
            )!.value;

          const combined = {
            ...states[0],
            use: Number(hidden(`${incrFileWindow.name}.use`)),
            [HERE_VALUE]: hidden(incrFileWindow.name),
          } as ClientSettingState;

          setStates(states.map(() => combined));
          setSeparate(true);
        }}
      >
        Set a backup window per backup type
      </Button>
    </div>
  );
}

const windowSchema = (f: SettingField, use: ZodMiniType) => ({
  [f.name]: z
    .string()
    .check(z.regex(BACKUP_WINDOW_PATTERN, VALIDATION_MESSAGES.regex(f.label))),
  [`${f.name}.use`]: use,
});

const clientFields: SettingField[] = [
  {
    name: "memberof",
    label: "Group",
    kind: "custom",
    clientOnly: true,
    render: (settings) => <GroupMembership settings={settings} />,
    schema: { memberof: z.optional(z.string()) },
  },
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
  {
    name: incrFileWindow.name,
    label: "Backup window",
    kind: "custom",
    render: (settings, _, validationMessages) => (
      <BackupWindows
        settings={settings}
        validationMessages={validationMessages}
      />
    ),
    schema: Object.assign(
      windowSchema(combinedWindow, SOURCES),
      // The other inputs are empty while the combined one is shown
      ...otherWindows.map((f) => windowSchema(f, z.enum(["", "1", "2", "4"]))),
    ),
    toSettings: (entries) => {
      if (entries[`${otherWindows[0].name}.use`] !== "") {
        return entries;
      }

      const name = incrFileWindow.name;

      return Object.assign(
        { ...entries },
        ...otherWindows.map((f) => ({
          [f.name]: entries[name],
          [`${f.name}.use`]: entries[`${name}.use`],
        })),
      );
    },
  },
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
