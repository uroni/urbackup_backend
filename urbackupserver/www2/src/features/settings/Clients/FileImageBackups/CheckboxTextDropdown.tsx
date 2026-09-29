import { useRef, useState } from "react";
import {
  Field as FUIField,
  Input,
  InputProps,
} from "@fluentui/react-components";

import { CheckboxFieldUncontrolled } from "../../Form/CheckboxField";
import { type Field } from "../../Form/types";
import textStyles from "../../Form/TextField.module.css";
import styles from "./FileImageBackups.module.css";
import { ResettableInput } from "../../Form/TextField";
import { DropdownField } from "./DropdownField";
import {
  FORMATTED_VALUE_TO_USE,
  SETTINGS_SOURCE,
  USE_VALUES,
  VALUE_TO_USE,
} from "./utils";

type Use = 1 | 2 | 4;
// Numeric settings arrive from the server as strings
type SettingValue = number | string;

export interface InitialFormState {
  use: Use;
  value_group: SettingValue;
  value?: SettingValue;
  value_client?: SettingValue;
}

interface FormData {
  enabled: boolean;
  value: SettingValue | undefined; // Value shown in the input
  here: SettingValue | undefined; // Value for the "here" settings source
  use: Use;
}

const HERE_SETTINGS = USE_VALUES[SETTINGS_SOURCE.HERE];

function selectValueInUse(multiValue: InitialFormState, use: Use) {
  return multiValue[VALUE_TO_USE[use]];
}

function initialValue(
  initialFormState: InitialFormState,
  {
    field,
    isEnabled,
  }: {
    field: Field<string>;
    isEnabled?: boolean;
  },
): FormData {
  const { use } = initialFormState;

  const selectedValue = selectValueInUse(initialFormState, use);
  const hereValue = selectValueInUse(initialFormState, HERE_SETTINGS);

  // Handle cases when client setting has the value set as NaN
  if (selectedValue === undefined || isNaN(Number(selectedValue))) {
    return {
      enabled: isEnabled ?? true, // Show the fields to allow changing
      value: selectedValue,
      here: hereValue,
      use,
    };
  }

  const enabled = isEnabled ?? Number(selectedValue) >= 0;

  if (field.transformer?.ui) {
    return {
      enabled,
      value: field.transformer.ui(Number(selectedValue)),
      here: field.transformer.ui(Number(hereValue)),
      use,
    };
  }

  return {
    enabled,
    value: selectedValue,
    here: hereValue,
    use,
  };
}

export function CheckboxTextDropdown({
  name,
  checkboxLabel,
  dropdownLabel,
  field,
  validationMessage,
  initialFormState,
}: {
  name: string;
  checkboxLabel?: string;
  dropdownLabel: string;
  field: Field<string>;
  initialFormState: InitialFormState;
  validationMessage?: string;
}) {
  const nameUse = `${name}.use`;

  const [formData, setFormData] = useState(() =>
    initialValue(initialFormState, {
      field,
      // If no checkbox, keep the fields enabled by default
      isEnabled: checkboxLabel ? undefined : true,
    }),
  );

  const checky = {
    checkbox: {
      name: `enable_${name}`,
      label: checkboxLabel,
      value: formData.enabled,
    },
    field: {
      ...field,
      type: "number",
      value: formData.value,
    },
    dropdown: {
      name: nameUse,
      label: dropdownLabel,
      value: formData.use,
    },
  };

  const getHereValue = () => {
    // Send no changes to "HERE" value if the selected source is not HERE.
    // Even if the field was interacted with.
    if (formData.use != HERE_SETTINGS) {
      return selectValueInUse(initialFormState, HERE_SETTINGS);
    }

    return field.transformer?.api(Number(formData.here)) ?? formData.here;
  };

  const hiddenInputValues = {
    value: getHereValue(),
    use: formData.use,
  };

  return (
    <div className={styles["checkbox-wrapper"]}>
      {checkboxLabel && (
        <CheckboxFieldUncontrolled
          key={checky.checkbox.name}
          id={checky.checkbox.name}
          label={checky.checkbox.label}
          checked={formData.enabled}
          onChange={(_, d) => {
            const newChecked = d.checked as boolean;
            const newValue = newChecked
              ? Math.abs(Number(formData.value))
              : Math.abs(Number(formData.value)) * -1;

            setFormData({
              enabled: newChecked,
              value: newValue,
              here: newValue,
              use: HERE_SETTINGS,
            });
          }}
        />
      )}
      <input type="hidden" name={name} value={hiddenInputValues.value} />
      <input type="hidden" name={nameUse} value={hiddenInputValues.use} />
      {formData.enabled && (
        <div>
          <TextFieldDropdown
            key={checky.field.name}
            label={checky.field.label}
            validationMessage={validationMessage}
            type="text"
            inputProps={{
              ...checky.field.inputProps,
              value:
                formData.value === undefined
                  ? undefined
                  : String(formData.value),
            }}
            onChange={(newValue) => {
              setFormData((p) => ({
                ...p,
                value: newValue,
                here: newValue,
                // Switch used settings to HERE if input is changed
                use: HERE_SETTINGS,
              }));
            }}
          >
            <DropdownField
              id={nameUse}
              label={dropdownLabel}
              options={VALUE_TO_USE}
              formattedOptions={FORMATTED_VALUE_TO_USE}
              value={FORMATTED_VALUE_TO_USE[VALUE_TO_USE[formData.use]].name}
              selectedOptions={[String(formData.use)]}
              onOptionSelect={(e, d) => {
                const newUse = Number(d.optionValue) as Use;

                const newValue =
                  selectValueInUse(initialFormState, newUse) ?? "";

                const transformedValue =
                  field.transformer?.ui(Number(newValue)) ?? newValue;

                setFormData((p) => ({
                  ...p,
                  value: transformedValue,
                  use: newUse,
                }));
              }}
            />
          </TextFieldDropdown>
        </div>
      )}
    </div>
  );
}

function TextFieldDropdown({
  label,
  name,
  type,
  onChange,
  validationMessage,
  inputProps,
  children,
}: {
  label: string;
  name?: string;
  type: InputProps["type"];
  onChange: (value: string | undefined) => void;
  validationMessage?: string;
  inputProps?: Omit<InputProps, "onChange">;
  children?: React.ReactNode;
}) {
  const initialValue = useRef(inputProps?.value);

  const [_value, setValue] = useState(initialValue.current);

  const inputValue = inputProps?.value ?? _value;

  const isResetHidden = _value == initialValue.current;

  const resetToInitialValue = () => {
    {
      const newValue = initialValue.current;

      setValue(newValue);
      onChange(newValue);
    }
  };

  return (
    <FUIField
      label={label}
      {...(validationMessage && {
        validationState: "error",
        validationMessage,
      })}
      orientation="horizontal"
      className={textStyles.field}
    >
      <ResettableInput hidden={isResetHidden} onReset={resetToInitialValue}>
        <Input
          name={name}
          type={type}
          {...inputProps}
          value={inputValue}
          onChange={(e, data) => {
            const { value } = data;
            setValue(value);
            onChange(value);
          }}
        />
        {children}
      </ResettableInput>
    </FUIField>
  );
}
