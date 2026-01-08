import { Field, Checkbox, CheckboxProps } from "@fluentui/react-components";
import { useState } from "react";

export function CheckboxField({
  label,
  id,
  name,
  defaultChecked,
  onChange,
}: {
  label: string;
  id: string;
  name: string;
  defaultChecked: boolean;
  onChange: (value: boolean) => void;
}) {
  const [value, setValue] = useState(defaultChecked);

  return (
    <Field>
      <Checkbox
        id={id}
        label={label}
        name={name}
        checked={value}
        onChange={(_, data) => {
          const newValue = Boolean(data.checked);

          setValue(newValue);
          onChange(newValue);
        }}
      />
    </Field>
  );
}

export function CheckboxFieldUncontrolled(props: CheckboxProps) {
  return (
    <Field>
      <Checkbox {...props} />
    </Field>
  );
}
