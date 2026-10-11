import {
  Body1,
  Dropdown,
  DropdownProps,
  Label,
  Option,
} from "@fluentui/react-components";

import styles from "./DropdownField.module.css";
import { FORMATTED_VALUE_TO_USE, VALUE_TO_USE } from "./utils";

export function DropdownField({
  label,
  options,
  formattedOptions,
  ...props
}: {
  label: string;
  options: typeof VALUE_TO_USE;
  // May leave out sources that aren't offered, e.g. the client value
  formattedOptions: Partial<typeof FORMATTED_VALUE_TO_USE>;
} & Partial<DropdownProps>) {
  return (
    <div>
      <Label htmlFor={props.id} className="visually-hidden">
        {label}
      </Label>
      <Dropdown
        // Set explicitly to prevent validation of parent Field affecting this field
        aria-invalid={false}
        onOptionSelect={(e, d) => {
          props?.onOptionSelect?.(e, d);
        }}
        positioning="below-end"
        inlinePopup
        className={styles.dropdown}
        listbox={{
          className: styles.listbox,
        }}
        {...props}
      >
        {Object.entries(options).map(([k, v]) => {
          const option = formattedOptions[v];

          if (!option) {
            return null;
          }

          return (
            <Option key={k} value={k} text={option.name}>
              <div className={styles.option}>
                <Body1 block>{option.name}</Body1>
                <Body1 as="p">{option.desc}</Body1>
              </div>
            </Option>
          );
        })}
      </Dropdown>
    </div>
  );
}
