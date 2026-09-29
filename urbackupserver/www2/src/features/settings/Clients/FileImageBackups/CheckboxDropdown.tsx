import { useState } from "react";

import { CheckboxFieldUncontrolled } from "../../Form/CheckboxField";
import { type Field } from "../../Form/types";
import { DropdownField } from "./DropdownField";
import {
  FORMATTED_VALUE_TO_USE,
  SETTINGS_SOURCE,
  USE_VALUES,
  VALUE_TO_USE,
} from "./utils";
import styles from "./FileImageBackups.module.css";

type Use = 1 | 2;

export interface InitialFormState {
  use: Use;
  value_group: boolean;
  value?: boolean;
}

const { [USE_VALUES[SETTINGS_SOURCE.CLIENT]]: clientKey, ...dropdownOptions } =
  VALUE_TO_USE;
const { value_client, ...dropdownFormattedOptions } = FORMATTED_VALUE_TO_USE;

const HERE_SETTINGS = USE_VALUES[SETTINGS_SOURCE.HERE];

function selectValueInUse(multiValue: InitialFormState, use: Use) {
  const key = VALUE_TO_USE[use];

  // The client value isn't offered for this field
  if (key === "value_client") {
    return false;
  }

  return !!multiValue[key];
}

function initialValue(initialFormState: InitialFormState) {
  return {
    checked: selectValueInUse(initialFormState, initialFormState.use),
    here: selectValueInUse(initialFormState, HERE_SETTINGS),
    use: initialFormState.use,
  };
}

export function CheckboxDropdown({
  name,
  dropdownLabel,
  field,
  initialFormState,
}: {
  name: string;
  dropdownLabel: string;
  field: Field<string>;
  initialFormState: InitialFormState;
}) {
  const nameUse = `${name}.use`;

  const [formData, setFormData] = useState(() =>
    initialValue(initialFormState),
  );

  const getHereValue = () => {
    // Send no changes to "HERE" value if the selected source is not HERE.
    // Even if the field was interacted with.
    if (formData.use !== HERE_SETTINGS) {
      return selectValueInUse(initialFormState, HERE_SETTINGS);
    }

    return formData.here;
  };

  const hiddenInputValues = {
    value: String(getHereValue()),
    use: formData.use,
  };

  return (
    <div className={`${styles["checkbox-wrapper"]} repel`}>
      <input type="hidden" name={name} value={hiddenInputValues.value} />
      <input type="hidden" name={nameUse} value={hiddenInputValues.use} />
      <CheckboxFieldUncontrolled
        id={name}
        label={field.label}
        checked={formData.checked}
        onChange={(_, d) => {
          const newChecked = d.checked as boolean;

          setFormData({
            checked: newChecked,
            here: newChecked,
            use: HERE_SETTINGS,
          });
        }}
      />
      <DropdownField
        id={nameUse}
        label={dropdownLabel}
        options={dropdownOptions}
        formattedOptions={dropdownFormattedOptions}
        value={FORMATTED_VALUE_TO_USE[VALUE_TO_USE[formData.use]].name}
        selectedOptions={[String(formData.use)]}
        onOptionSelect={(_, d) => {
          const newUse = Number(d.optionValue) as Use;

          setFormData((p) => ({
            ...p,
            checked: selectValueInUse(initialFormState, newUse),
            use: newUse,
          }));
        }}
      />
    </div>
  );
}
