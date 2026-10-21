import { useState } from "react";
import { Field as FUIField, Select } from "@fluentui/react-components";

import textStyles from "../Form/TextField.module.css";
import styles from "./SelectDropdown.module.css";
import { DropdownField } from "./FileImageBackups/DropdownField";
import { isClientConfigurable } from "./FileImageBackups/clientConfigurable";
import {
  FORMATTED_VALUE_TO_USE,
  SETTINGS_SOURCE,
  USE_VALUES,
  VALUE_TO_USE,
} from "./FileImageBackups/utils";

type Use = 1 | 2 | 4;

export interface InitialFormState {
  use: Use;
  value_group: string;
  value?: string;
  value_client?: string;
}

const HERE_SETTINGS = USE_VALUES[SETTINGS_SOURCE.HERE];

const { value_client, ...formattedOptionsWithoutClient } =
  FORMATTED_VALUE_TO_USE;

function selectValueInUse(multiValue: InitialFormState, use: Use) {
  return multiValue[VALUE_TO_USE[use]];
}

/**
 * A setting with a fixed list of values and a settings source selector
 */
export function SelectDropdown({
  name,
  label,
  dropdownLabel,
  options,
  initialFormState,
}: {
  name: string;
  label: string;
  dropdownLabel: string;
  options: { value: string; label: string }[];
  initialFormState: InitialFormState;
}) {
  const nameUse = `${name}.use`;

  const [formData, setFormData] = useState(() => ({
    value: selectValueInUse(initialFormState, initialFormState.use),
    here: selectValueInUse(initialFormState, HERE_SETTINGS),
    use: initialFormState.use,
  }));

  // Send no changes to the "here" value if another source is selected
  const hereValue =
    formData.use === HERE_SETTINGS
      ? formData.here
      : selectValueInUse(initialFormState, HERE_SETTINGS);

  return (
    <div>
      <input type="hidden" name={name} value={hereValue ?? ""} />
      <input type="hidden" name={nameUse} value={formData.use} />
      <FUIField
        label={label}
        orientation="horizontal"
        className={`${textStyles.field} ${styles.field}`}
      >
        <div className={styles.row}>
          <Select
            className={styles.select}
            value={formData.value ?? ""}
            onChange={(_, d) =>
              setFormData({ value: d.value, here: d.value, use: HERE_SETTINGS })
            }
          >
            {options.map((o) => (
              <option key={o.value} value={o.value}>
                {o.label}
              </option>
            ))}
          </Select>
          <DropdownField
            id={nameUse}
            label={dropdownLabel}
            options={VALUE_TO_USE}
            formattedOptions={
              isClientConfigurable(name)
                ? FORMATTED_VALUE_TO_USE
                : formattedOptionsWithoutClient
            }
            value={FORMATTED_VALUE_TO_USE[VALUE_TO_USE[formData.use]].name}
            selectedOptions={[String(formData.use)]}
            onOptionSelect={(_, d) => {
              const newUse = Number(d.optionValue) as Use;

              setFormData((p) => {
                // Keep the shown value if the source has none yet
                const value =
                  selectValueInUse(initialFormState, newUse) ?? p.value;

                return {
                  value,
                  here: newUse === HERE_SETTINGS ? value : p.here,
                  use: newUse,
                };
              });
            }}
          />
        </div>
      </FUIField>
    </div>
  );
}
