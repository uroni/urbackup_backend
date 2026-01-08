import { useRef, useState } from "react";
import { Field, Input, InputProps } from "@fluentui/react-components";

import type { ClientSettingState } from "../../../../api/urbackupserver";
import { CheckboxFieldUncontrolled } from "../../Form/CheckboxField";
import {
  type EnabledState,
  getEnabledState,
  getUse,
  SETTINGS_SOURCE,
} from "./utils";
import { ResettableInput } from "../../Form/TextField";
import styles from "./FileImageBackups.module.css";

export function MergeableFields({
  name,
  label,
  initialFormState,
}: {
  name: string;
  label: string;
  initialFormState: ClientSettingState;
}) {
  const nameUse = `${name}.use`;

  const [hereValue, setHereValue] = useState<string>(
    initialFormState.value as string,
  );

  const [enabledStates, setEnabledStates] = useState<EnabledState[]>(() =>
    getEnabledState(initialFormState.use ?? 0),
  );

  const getHereValue = () => {
    const isHereValueEnabled = isSourceEnabled(
      enabledStates,
      SETTINGS_SOURCE.HERE,
    );

    // Send no changes to "HERE" value if the selected source is not HERE.
    // Even if the field was interacted with.
    if (!isHereValueEnabled) {
      return initialFormState.value;
    }

    return hereValue;
  };

  const updateUse = (
    source: (typeof SETTINGS_SOURCE)[keyof typeof SETTINGS_SOURCE],
    value: boolean,
  ) => {
    setEnabledStates((p) => {
      return p.reduce((all, curr) => {
        if (curr.name !== source) {
          return [...all, curr];
        }

        const updatedSource: EnabledState = {
          ...curr,
          enabled: +value as 0 | 1,
        };

        return [...all, updatedSource];
      }, [] as EnabledState[]);
    });
  };

  const hiddenInputValues = {
    value: getHereValue(),
    use: getUse(enabledStates),
  };

  const inputs = [
    {
      name: SETTINGS_SOURCE.HERE,
      labels: makeLabels(SETTINGS_SOURCE.HERE, label),
      state: isSourceEnabled(enabledStates, SETTINGS_SOURCE.HERE),
      value: hiddenInputValues.value,
    },
    {
      name: SETTINGS_SOURCE.GROUP,
      labels: makeLabels(SETTINGS_SOURCE.GROUP, label),
      readOnly: true,
      state: isSourceEnabled(enabledStates, SETTINGS_SOURCE.GROUP),
      value: initialFormState.value_group,
    },
    {
      name: SETTINGS_SOURCE.CLIENT,
      labels: makeLabels(SETTINGS_SOURCE.CLIENT, label),
      readOnly: true,
      state: isSourceEnabled(enabledStates, SETTINGS_SOURCE.CLIENT),
      value: initialFormState.value_client,
    },
  ];

  return (
    <>
      <input
        type="hidden"
        name={name}
        value={String(hiddenInputValues.value)}
      />
      <input type="hidden" name={nameUse} value={hiddenInputValues.use} />
      <div className={`${styles.mergeable} flow flow-s`}>
        {inputs.map((input) => (
          <CheckboxTextField
            key={input.name}
            checked={input.state}
            checkboxLabel={input.labels.checkbox}
            onCheckboxChange={(checked) => {
              updateUse(input.name, checked);
            }}
          >
            <MergeableTextField
              label={input.labels.input}
              defaultValue={String(input.value)}
              type="text"
              readOnly={input?.readOnly}
              onChange={(value) => {
                setHereValue(value);
              }}
            />
          </CheckboxTextField>
        ))}
      </div>
    </>
  );
}

function CheckboxTextField({
  checked,
  checkboxLabel,
  onCheckboxChange,
  children,
}: {
  checked: boolean;
  checkboxLabel: string;
  onCheckboxChange: (checked: boolean) => void;
  children: React.ReactNode;
}) {
  const [enabled, setEnabled] = useState(checked);

  return (
    <div className={`${styles["mergeable__row"]} repel`}>
      <CheckboxFieldUncontrolled
        label={checkboxLabel}
        checked={enabled}
        onChange={(_, d) => {
          setEnabled(d.checked as boolean);
          onCheckboxChange(d.checked as boolean);
        }}
      />

      {enabled && children}
    </div>
  );
}

function MergeableTextField({
  label,
  defaultValue,
  type,
  readOnly,
  onChange,
}: {
  label: string;
  defaultValue: string;
  type: InputProps["type"];
  readOnly?: boolean;
  onChange: (value: string) => void;
}) {
  const initialValue = useRef(defaultValue);

  const [value, setValue] = useState(defaultValue);

  const isResetHidden = readOnly || value == initialValue.current;

  const resetToInitialValue = () => {
    {
      const newValue = initialValue.current;

      setValue(newValue);
      onChange(newValue);
    }
  };

  const labelValue = {
    children: label,
    className: "visually-hidden",
  };

  return (
    <Field label={labelValue} orientation="vertical">
      <ResettableInput hidden={isResetHidden} onReset={resetToInitialValue}>
        <Input
          type={type}
          readOnly={readOnly}
          value={value}
          onChange={(_, data) => {
            const { value } = data;
            setValue(value);
            onChange?.(value);
          }}
        />
      </ResettableInput>
    </Field>
  );
}

function isSourceEnabled(
  enabledStates: EnabledState[],
  source: (typeof SETTINGS_SOURCE)[keyof typeof SETTINGS_SOURCE],
) {
  const state = enabledStates.find((es) => es.name === source);

  return Boolean(state!.enabled);
}

function makeLabels(
  source: (typeof SETTINGS_SOURCE)[keyof typeof SETTINGS_SOURCE],
  label: string,
) {
  return {
    checkbox: `Apply setting from ${source} for ${label}`,
    input: `Settings for ${label} from ${source}`,
  };
}
