import { useState } from "react";
import {
  Checkbox,
  Field as FUIField,
  Input,
  Select,
} from "@fluentui/react-components";
import { z } from "zod/v4-mini";

import type {
  ClientSettings,
  SettingsAlertScript,
} from "../../../api/urbackupserver";
import textStyles from "../Form/TextField.module.css";
import { ClientSettingsTab, type SettingField } from "./ClientSettingsTab";
import { DropdownField } from "./FileImageBackups/DropdownField";
import {
  FORMATTED_VALUE_TO_USE,
  SETTINGS_SOURCE,
  USE_VALUES,
  VALUE_TO_USE,
} from "./FileImageBackups/utils";
import { createDropdownLabel } from "./FileImageBackups/FileImageBackups";
import selectStyles from "./SelectDropdown.module.css";

// English texts of the translated parameter labels of the old web interface
const PARAM_LABELS: Record<string, string> = {
  alert_file_mult:
    "Number of times the file backup interval after which the file backup status is set to not ok",
  alert_image_mult:
    "Number of times the image backup interval after which the image backup status is set to not ok",
  alert_emails:
    "E-Mail addresses (separated by ';') to notify once the image/file backup status is not ok",
  alert_important: "Add [Important] tag to alert mails",
  alert_mail_ok: "Send ok mail after backup status goes from not ok to ok",
  alert_nag_interval:
    "Repeat the alert mail at this interval while there is no successful backup (0: never)",
};

const INTERVAL_UNITS = {
  ms: { label: "milliseconds", ms: 1 },
  s: { label: "seconds", ms: 1000 },
  m: { label: "minutes", ms: 1000 * 60 },
  h: { label: "hours", ms: 1000 * 60 * 60 },
  d: { label: "days", ms: 1000 * 60 * 60 * 24 },
  M: { label: "months", ms: 1000 * 60 * 60 * 24 * 30 },
  y: { label: "years", ms: 1000 * 60 * 60 * 24 * 365 },
};

type IntervalUnit = keyof typeof INTERVAL_UNITS;

type Use = 1 | 2;

const HERE = USE_VALUES[SETTINGS_SOURCE.HERE];

const { value_client, ...sourceOptions } = FORMATTED_VALUE_TO_USE;

type Setting = { use: Use; value?: unknown; value_group?: unknown };

function sourceValue(setting: Setting | undefined, use: Use) {
  const value = setting?.[VALUE_TO_USE[use] as "value" | "value_group"];
  return value === undefined ? undefined : String(value);
}

function paramLabel(param: SettingsAlertScript["params"][number]) {
  if (!param.has_translation) {
    return param.label;
  }

  return (
    PARAM_LABELS[param.label] ??
    param.label.replace(/^pulseway_/, "").replaceAll("_", " ")
  );
}

function AlertSettings({ settings }: { settings: ClientSettings["settings"] }) {
  const all = settings as Record<string, unknown>;
  const scripts = (all.alert_scripts ?? []) as SettingsAlertScript[];
  const scriptSetting = all.alert_script as Setting;
  const paramsSetting = all.alert_params as Setting;

  const stateFor = (use: Use) => ({
    use,
    script: sourceValue(scriptSetting, use) ?? String(scripts[0]?.id ?? ""),
    params: Object.fromEntries(
      new URLSearchParams(sourceValue(paramsSetting, use) ?? ""),
    ),
  });

  const [state, setState] = useState(() =>
    stateFor(scriptSetting?.use === HERE ? HERE : 1),
  );

  // Shows the values of a newly selected source in the inputs
  const [sourceSwitches, setSourceSwitches] = useState(0);

  const script = scripts.find((s) => String(s.id) === state.script);

  const change = (update: Partial<typeof state>) =>
    setState((p) => ({ ...p, ...update, use: HERE }));

  const setParam = (name: string, value: string) =>
    change({ params: { ...state.params, [name]: value } });

  // Every parameter of the selected script, with its default if it isn't set
  const params = Object.fromEntries(
    (script?.params ?? []).map((p) => [
      p.name,
      state.params[p.name] ??
        // The default of a choice lists the choices, the first is selected
        (p.type === "choice" ? p.default_value.split("|")[0] : p.default_value),
    ]),
  );

  // Send no changes to the "here" values if the group values are used
  const hidden =
    state.use === HERE
      ? {
          script: state.script,
          params: new URLSearchParams(params).toString(),
        }
      : {
          script: sourceValue(scriptSetting, HERE) ?? "",
          params: sourceValue(paramsSetting, HERE) ?? "",
        };

  return (
    <div className="flow">
      <input type="hidden" name="alert_script" value={hidden.script} />
      <input type="hidden" name="alert_script.use" value={state.use} />
      <input type="hidden" name="alert_params" value={hidden.params} />
      <input type="hidden" name="alert_params.use" value={state.use} />

      <FUIField
        label="Alert script"
        orientation="horizontal"
        className={`${textStyles.field} ${selectStyles.field}`}
      >
        <div className={selectStyles.row}>
          <Select
            className={selectStyles.select}
            value={state.script}
            onChange={(_, d) => change({ script: d.value })}
          >
            {scripts.map((s) => (
              <option key={s.id} value={String(s.id)}>
                {s.name}
              </option>
            ))}
          </Select>
          <DropdownField
            id="alert_script.use"
            label={createDropdownLabel("alert script")}
            options={VALUE_TO_USE}
            formattedOptions={sourceOptions}
            value={FORMATTED_VALUE_TO_USE[VALUE_TO_USE[state.use]].name}
            selectedOptions={[String(state.use)]}
            onOptionSelect={(_, d) => {
              const use = Number(d.optionValue) as Use;
              setSourceSwitches((n) => n + 1);
              const fromSource = stateFor(use);

              // Keep what is shown if the source has nothing yet
              setState((p) => ({
                use,
                script:
                  sourceValue(scriptSetting, use) === undefined
                    ? p.script
                    : fromSource.script,
                params:
                  sourceValue(paramsSetting, use) === undefined
                    ? params
                    : fromSource.params,
              }));
            }}
          />
        </div>
      </FUIField>

      {script?.params.map((param) => (
        <AlertParam
          key={`${sourceSwitches}-${script.id}-${param.name}`}
          param={param}
          value={params[param.name]}
          onChange={(v) => setParam(param.name, v)}
        />
      ))}
    </div>
  );
}

function AlertParam({
  param,
  value,
  onChange,
}: {
  param: SettingsAlertScript["params"][number];
  value: string;
  onChange: (value: string) => void;
}) {
  const label = paramLabel(param);

  if (param.type === "bool") {
    return (
      <Checkbox
        label={label}
        checked={value !== "0"}
        onChange={(_, d) => onChange(d.checked ? "1" : "0")}
      />
    );
  }

  return (
    <FUIField
      label={label}
      orientation="horizontal"
      className={`${textStyles.field} ${selectStyles.field}`}
    >
      <div className={selectStyles.row}>
        {param.type === "choice" ? (
          <Select
            className={selectStyles.select}
            value={value}
            onChange={(_, d) => onChange(d.value)}
          >
            {param.default_value.split("|").map((c) => (
              <option key={c}>{c}</option>
            ))}
          </Select>
        ) : param.type === "interval" ? (
          <IntervalInput value={value} onChange={onChange} />
        ) : (
          <Input
            className={selectStyles.select}
            value={value}
            onChange={(_, d) => {
              // Numbers can only be typed as such, like the checks of the old UI
              const valid =
                param.type === "int"
                  ? /^-?\d*$/
                  : param.type === "num"
                    ? /^-?\d*\.?\d*$/
                    : /.*/;

              if (valid.test(d.value)) {
                onChange(d.value);
              }
            }}
          />
        )}
      </div>
    </FUIField>
  );
}

/**
 * An interval stored as "<milliseconds>|<unit>" and shown in the unit
 */
function IntervalInput({
  value,
  onChange,
}: {
  value: string;
  onChange: (value: string) => void;
}) {
  const [ms = "0", storedUnit = "h"] = value.split("|");
  const unit = (
    storedUnit in INTERVAL_UNITS ? storedUnit : "h"
  ) as IntervalUnit;

  const [shown, setShown] = useState(() =>
    String(Number(ms) / INTERVAL_UNITS[unit].ms),
  );

  return (
    <>
      <Input
        className={selectStyles.select}
        value={shown}
        onChange={(_, d) => {
          if (!/^\d*\.?\d*$/.test(d.value)) {
            return;
          }

          setShown(d.value);
          onChange(
            `${Math.round(Number(d.value) * INTERVAL_UNITS[unit].ms)}|${unit}`,
          );
        }}
      />
      <Select
        value={unit}
        onChange={(_, d) => {
          const newUnit = d.value as IntervalUnit;
          setShown(String(Number(ms) / INTERVAL_UNITS[newUnit].ms));
          onChange(`${ms}|${newUnit}`);
        }}
      >
        {Object.entries(INTERVAL_UNITS).map(([k, u]) => (
          <option key={k} value={k}>
            {u.label}
          </option>
        ))}
      </Select>
    </>
  );
}

const alertFields: SettingField[] = [
  {
    name: "alert_script",
    label: "Alert script",
    kind: "custom",
    render: (settings) => <AlertSettings settings={settings} />,
    schema: {
      alert_script: z.string(),
      "alert_script.use": z.enum(["1", "2"]),
      alert_params: z.string(),
      "alert_params.use": z.enum(["1", "2"]),
    },
  },
];

export function AlertsTab() {
  return <ClientSettingsTab title="Alerts" fields={alertFields} />;
}
