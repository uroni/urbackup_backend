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

interface InitialFormState {
  use: 1 | 2;
  value_group: boolean;
  value?: boolean;
}

const { [USE_VALUES[SETTINGS_SOURCE.CLIENT]]: clientKey, ...dropdownOptions } =
  VALUE_TO_USE;
const { value_client, ...dropdownFormattedOptions } = FORMATTED_VALUE_TO_USE;

const HERE_SETTINGS = USE_VALUES[SETTINGS_SOURCE.HERE];

function selectValueInUse(multiValue: InitialFormState) {
  const { use } = multiValue;

  return !!multiValue[VALUE_TO_USE[use]];
}

function initialValue(
  initialFormState: InitialFormState,
  {
    name,
    nameUse,
  }: {
    name: string;
    nameUse: string;
  },
) {
  const { use } = initialFormState;

  const selectedValue = selectValueInUse(initialFormState);
  const hereValue = selectValueInUse({
    ...initialFormState,
    use: HERE_SETTINGS,
  });

  return {
    [name]: selectedValue,
    here: hereValue,
    [nameUse]: use,
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
    initialValue(initialFormState, { name, nameUse }),
  );

  const getHereValue = () => {
    // Send no changes to "HERE" value if the selected source is not HERE.
    // Even if the field was interacted with.
    if (+formData[nameUse] !== HERE_SETTINGS) {
      const hereValue = selectValueInUse({
        ...initialFormState,
        use: HERE_SETTINGS,
      });

      return hereValue;
    }

    return formData["here"];
  };

  const hiddenInputValues = {
    value: getHereValue(),
    use: formData[nameUse],
  };

  return (
    <div className={`${styles["checkbox-wrapper"]} repel`}>
      <input type="hidden" name={name} value={hiddenInputValues.value} />
      <input type="hidden" name={nameUse} value={hiddenInputValues.use} />
      <CheckboxFieldUncontrolled
        id={name}
        label={field.label}
        checked={formData[name]}
        onChange={(_, d) => {
          const newChecked = d.checked as boolean;

          setFormData((p) => ({
            ...p,
            [name]: newChecked,
            here: newChecked,
            [nameUse]: HERE_SETTINGS as InitialFormState["use"],
          }));
        }}
      />
      <DropdownField
        id={nameUse}
        label={dropdownLabel}
        options={dropdownOptions}
        formattedOptions={dropdownFormattedOptions}
        value={FORMATTED_VALUE_TO_USE[VALUE_TO_USE[formData[nameUse]]].name}
        selectedOptions={[String(formData[nameUse])]}
        onOptionSelect={(_, d) => {
          const newUse = d.optionValue;

          const newValue = selectValueInUse({
            ...initialFormState,
            use: newUse,
          });

          setFormData((p) => ({
            ...p,
            [name]: newValue,
            [nameUse]: newUse,
          }));
        }}
      />
    </div>
  );
}
