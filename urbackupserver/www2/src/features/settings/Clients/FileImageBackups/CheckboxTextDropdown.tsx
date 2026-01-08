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

interface InitialFormState {
  use: 1 | 2 | 4;
  value_group: number;
  value?: number;
  value_client?: number;
}

const HERE_SETTINGS = USE_VALUES[SETTINGS_SOURCE.HERE];

function selectValueInUse(multiValue: InitialFormState) {
  const { use } = multiValue;

  return multiValue[VALUE_TO_USE[use]];
}

function initialValue(
  initialFormState: InitialFormState,
  {
    field,
    name,
    nameUse,
    isEnabled,
  }: {
    field: Field<string>;
    name: string;
    nameUse: string;
    isEnabled?: boolean;
  },
) {
  const { use } = initialFormState;

  const selectedValue = selectValueInUse(initialFormState);
  const hereValue = selectValueInUse({
    ...initialFormState,
    use: HERE_SETTINGS,
  });

  // Handle cases when client setting has the value set as NaN
  if (isNaN(selectedValue)) {
    return {
      enabled: isEnabled ?? true, // Show the fields to allow changing
      [name]: selectedValue,
      here: hereValue,
      [nameUse]: use,
    };
  }

  const enabled = isEnabled ?? selectedValue >= 0;

  if (field.transformer?.ui) {
    return {
      enabled,
      [name]: field.transformer?.ui(selectedValue),
      here: field.transformer?.ui(hereValue),
      [nameUse]: use,
    };
  }

  return {
    enabled,
    [name]: selectedValue,
    here: hereValue,
    [nameUse]: use,
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
      name,
      nameUse,
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
      value: formData[name],
    },
    dropdown: {
      name: nameUse,
      label: dropdownLabel,
      value: formData[nameUse],
    },
  };

  const getHereValue = () => {
    // Send no changes to "HERE" value if the selected source is not HERE.
    // Even if the field was interacted with.
    if (formData[nameUse] != HERE_SETTINGS) {
      const hereValue = selectValueInUse({
        ...initialFormState,
        use: HERE_SETTINGS,
      });

      return hereValue;
    }

    return field.transformer?.api(formData["here"]) ?? formData["here"];
  };

  const hiddenInputValues = {
    value: getHereValue(),
    use: formData[nameUse],
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
              ? Math.abs(+formData[name])
              : Math.abs(+formData[name]) * -1;

            setFormData({
              enabled: newChecked,
              [name]: newValue,
              here: newValue,
              [nameUse]: HERE_SETTINGS,
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
            description={checky.field.description}
            validationMessage={validationMessage}
            type="text"
            inputProps={{
              ...checky.field.inputProps,
              value: formData[name],
            }}
            onChange={(newValue) => {
              setFormData((p) => ({
                ...p,
                [name]: newValue,
                here: newValue,
                // Switch used settings to HERE if input is changed
                [nameUse]: HERE_SETTINGS,
              }));
            }}
          >
            <DropdownField
              id={nameUse}
              label={dropdownLabel}
              options={VALUE_TO_USE}
              formattedOptions={FORMATTED_VALUE_TO_USE}
              value={
                FORMATTED_VALUE_TO_USE[VALUE_TO_USE[formData[nameUse]]].name
              }
              selectedOptions={[String(formData[nameUse])]}
              onOptionSelect={(e, d) => {
                const newUse = d.optionValue;

                const newValue =
                  selectValueInUse({
                    ...initialFormState,
                    use: newUse,
                  }) ?? "";

                const transformedValue =
                  field.transformer?.ui(+newValue) ?? newValue;

                setFormData((p) => ({
                  ...p,
                  [name]: transformedValue,
                  [nameUse]: newUse,
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
  name: string;
  type: InputProps["type"];
  onChange: (value: string) => void;
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
