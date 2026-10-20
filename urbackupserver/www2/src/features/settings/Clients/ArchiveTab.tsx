import { useContext, useState } from "react";
import {
  Body1,
  Button,
  Checkbox,
  Field as FUIField,
  Input,
  Select,
  Table,
  TableBody,
  TableCell,
  TableHeader,
  TableHeaderCell,
  TableRow,
} from "@fluentui/react-components";
import { DeleteRegular } from "@fluentui/react-icons";
import { z } from "zod/v4-mini";

import type { ClientSettings } from "../../../api/urbackupserver";
import { ClientSettingsTab, type SettingField } from "./ClientSettingsTab";
import {
  ARCHIVE_WINDOW_PATTERN,
  type ArchiveRule,
  parseArchiveRules,
  serializeArchiveRules,
  type TimeUnit,
} from "./archiveRules";
import { GlobalSettings } from "./FileImageBackups/utils";
import styles from "./ArchiveTab.module.css";

const GROUP = 1;
const HERE = 2;

const UNITS: Record<TimeUnit, [string, string]> = {
  h: ["hour", "hours"],
  d: ["day", "days"],
  w: ["week", "weeks"],
  m: ["month", "months"],
  y: ["year", "years"],
};

const BACKUP_TYPES: Record<string, string> = {
  file: "File backup",
  incr_file: "Incremental file backup",
  full_file: "Full file backup",
  image: "Image backup",
  incr_image: "Incremental image backup",
  full_image: "Full image backup",
};

type Setting = { use?: number; value?: string; value_group?: string };

type Row = ArchiveRule & { source: typeof GROUP | typeof HERE };

function formatLength(value: number, unit: TimeUnit | "i") {
  if (unit === "i") {
    return "forever";
  }

  return `${value} ${UNITS[unit][value === 1 ? 0 : 1]}`;
}

function ArchiveSettings({
  settings,
}: {
  settings: ClientSettings["settings"];
}) {
  const setting = ((settings as Record<string, unknown>).archive ??
    {}) as Setting;

  // The general rules have their own prefix and no group to use
  const global = useContext(GlobalSettings);
  const prefix = global ? "d" : "c";

  const [use, setUse] = useState(setting.use ?? HERE);

  const groupRules = parseArchiveRules(setting.value_group ?? "", ["d", "g"]);

  const [hereRules, setHereRules] = useState(() =>
    parseArchiveRules(setting.value ?? "", [prefix]),
  );

  const rows: Row[] = [
    ...(use & GROUP ? groupRules.map((r) => ({ ...r, source: GROUP })) : []),
    ...(use & HERE ? hereRules.map((r) => ({ ...r, source: HERE })) : []),
  ] as Row[];

  const remove = (index: number) => {
    const row = rows[index];

    if (row.source === GROUP) {
      // Like the old web interface: the group rules become rules of this
      // client, without the removed one
      setHereRules(rows.filter((_, i) => i !== index));
      setUse(HERE);
      return;
    }

    const hereIndex = index - (use & GROUP ? groupRules.length : 0);
    setHereRules((p) => p.filter((_, i) => i !== hereIndex));
  };

  const add = (rule: ArchiveRule) => {
    setHereRules((p) => [...p, rule]);
    setUse((u) => u | HERE);
  };

  // Without any source there are no rules, which is stored as no own rules
  const savedUse = use === 0 ? HERE : use;
  const savedValue =
    use & HERE ? serializeArchiveRules(hereRules, prefix) : setting.value ?? "";

  return (
    <div className="flow">
      <input type="hidden" name="archive" value={savedValue} />
      <input type="hidden" name="archive.use" value={savedUse} />

      {!global && (
        <Checkbox
          label="Use the archive rules of the group"
          checked={(use & GROUP) !== 0}
          onChange={(_, d) =>
            setUse((u) => (d.checked ? u | GROUP : u & ~GROUP))
          }
        />
      )}

      <Table size="small" className={styles.table}>
        <TableHeader>
          <TableRow>
            <TableHeaderCell>Archive every</TableHeaderCell>
            <TableHeaderCell>Archive for</TableHeaderCell>
            <TableHeaderCell>Archive window</TableHeaderCell>
            <TableHeaderCell>Backup type</TableHeaderCell>
            <TableHeaderCell>Volume letters</TableHeaderCell>
            {!global && <TableHeaderCell>Source</TableHeaderCell>}
            <TableHeaderCell>
              <span className="visually-hidden">Actions</span>
            </TableHeaderCell>
          </TableRow>
        </TableHeader>
        <TableBody>
          {rows.map((r, i) => (
            <TableRow key={`${r.source}-${i}`}>
              <TableCell>{formatLength(r.every, r.everyUnit)}</TableCell>
              <TableCell>{formatLength(r.for, r.forUnit)}</TableCell>
              <TableCell>{r.window}</TableCell>
              <TableCell>
                {BACKUP_TYPES[r.backupType] ?? r.backupType}
              </TableCell>
              <TableCell>{r.letters || "-"}</TableCell>
              {!global && (
                <TableCell>{r.source === GROUP ? "Group" : "Here"}</TableCell>
              )}
              <TableCell>
                <Button
                  appearance="subtle"
                  icon={<DeleteRegular />}
                  aria-label="Remove archive rule"
                  onClick={() => remove(i)}
                />
              </TableCell>
            </TableRow>
          ))}
          {rows.length === 0 && (
            <TableRow>
              <TableCell colSpan={global ? 6 : 7}>
                <Body1>No backups are archived automatically.</Body1>
              </TableCell>
            </TableRow>
          )}
        </TableBody>
      </Table>

      <NewArchiveRule onAdd={add} />
    </div>
  );
}

function NewArchiveRule({ onAdd }: { onAdd: (rule: ArchiveRule) => void }) {
  const [rule, setRule] = useState({
    every: "",
    everyUnit: "d" as TimeUnit,
    for: "",
    forUnit: "d" as TimeUnit | "i",
    backupType: "file",
    window: "*;*;*;*",
    letters: "ALL",
  });

  const [error, setError] = useState("");

  const set = (update: Partial<typeof rule>) =>
    setRule((p) => ({ ...p, ...update }));

  const isImage = rule.backupType.includes("image");

  const add = () => {
    if (!(Number(rule.every) > 0)) {
      return setError("Please enter how often to archive");
    }

    if (rule.forUnit !== "i" && !(Number(rule.for) > 0)) {
      return setError("Please enter how long to archive");
    }

    if (!ARCHIVE_WINDOW_PATTERN.test(rule.window)) {
      return setError("The format for archive window is invalid");
    }

    setError("");
    onAdd({
      ...rule,
      every: Number(rule.every),
      for: rule.forUnit === "i" ? 0 : Number(rule.for),
      letters: isImage ? rule.letters : "",
    });
    set({ every: "", for: "" });
  };

  const numeric = (v: string) => /^\d*\.?\d*$/.test(v);

  return (
    <fieldset className={`${styles["new-rule"]} flow`}>
      <legend>
        <Body1>Add archive rule</Body1>
      </legend>
      <div className={styles.inputs}>
        <FUIField label="Archive every">
          <div className={styles.length}>
            <Input
              value={rule.every}
              onChange={(_, d) => numeric(d.value) && set({ every: d.value })}
            />
            <Select
              value={rule.everyUnit}
              onChange={(_, d) => set({ everyUnit: d.value as TimeUnit })}
            >
              {Object.entries(UNITS).map(([k, [, plural]]) => (
                <option key={k} value={k}>
                  {plural}
                </option>
              ))}
            </Select>
          </div>
        </FUIField>
        <FUIField label="Archive for">
          <div className={styles.length}>
            {rule.forUnit !== "i" && (
              <Input
                value={rule.for}
                onChange={(_, d) => numeric(d.value) && set({ for: d.value })}
              />
            )}
            <Select
              value={rule.forUnit}
              onChange={(_, d) => set({ forUnit: d.value as TimeUnit | "i" })}
            >
              {Object.entries(UNITS).map(([k, [, plural]]) => (
                <option key={k} value={k}>
                  {plural}
                </option>
              ))}
              <option value="i">forever</option>
            </Select>
          </div>
        </FUIField>
        <FUIField
          label="Archive window"
          hint="Hour;day of month;month;day of week, e.g. *;1;*;* for the first day of each month"
        >
          <Input
            value={rule.window}
            onChange={(_, d) => set({ window: d.value })}
          />
        </FUIField>
        <FUIField label="Backup type">
          <Select
            value={rule.backupType}
            onChange={(_, d) => set({ backupType: d.value })}
          >
            {Object.entries(BACKUP_TYPES).map(([k, label]) => (
              <option key={k} value={k}>
                {label}
              </option>
            ))}
          </Select>
        </FUIField>
        <FUIField label="Volume letters">
          <Input
            value={isImage ? rule.letters : ""}
            disabled={!isImage}
            onChange={(_, d) => set({ letters: d.value })}
          />
        </FUIField>
      </div>
      {error && (
        <Body1 as="p" role="alert" className={styles.error}>
          {error}
        </Body1>
      )}
      <Button onClick={add}>Add</Button>
    </fieldset>
  );
}

const archiveFields: SettingField[] = [
  {
    name: "archive",
    label: "Archive",
    kind: "custom",
    render: (settings) => <ArchiveSettings settings={settings} />,
    schema: {
      archive: z.string(),
      "archive.use": z.enum(["1", "2", "3"]),
    },
  },
];

export function ArchiveTab() {
  return <ClientSettingsTab title="Archive" fields={archiveFields} />;
}
