import {
  Body1,
  Button,
  Field,
  Input,
  Label,
  LabelProps,
  type InputProps,
} from "@fluentui/react-components";
import { ArrowResetRegular } from "@fluentui/react-icons";
import { useState } from "react";
import type { ZodMiniType } from "zod/v4-mini";

import styles from "./TextField.module.css";

export function TextField({
  schema,
  label,
  description,
  hint,
  name,
  defaultValue,
  type,
  onChange,
  inputProps,
}: {
  schema: ZodMiniType;
  label: string;
  description: React.ReactNode;
  hint?: string;
  name: string;
  defaultValue: string;
  type: InputProps["type"];
  onChange: (value: unknown) => void;
  inputProps?: InputProps;
}) {
  const [validationMessage, setValidationMessage] = useState("");
  const [value, setValue] = useState(defaultValue);

  const showHint = !validationMessage;

  const handleChange = (newValue: string) => {
    const parsed = schema.safeParse(newValue);

    if (parsed.success) {
      const parsedValue = parsed.data;
      onChange(parsedValue);
    }
  };

  return (
    <Field
      hint={showHint ? hint : undefined}
      // @ts-expect-error: label takes both string and JSX.element -
      // remove when FluentUI components package is updated to reflect that
      label={{
        children: (_: unknown, slotProps: LabelProps) => (
          <div>
            <Label {...slotProps}>{label}</Label>
            {description && (
              <p className={styles["field-description"]}>
                <Body1>{description}</Body1>
              </p>
            )}
          </div>
        ),
      }}
      {...(validationMessage && {
        validationState: "error",
        validationMessage,
      })}
      orientation="horizontal"
      className={styles.field}
    >
      <div className={styles["reset-input"]}>
        <Button
          appearance="subtle"
          icon={<ArrowResetRegular />}
          data-hidden={value == defaultValue}
          onClick={() => {
            const value = String(defaultValue);

            setValue(value);
            setValidationMessage("");

            handleChange(value);
          }}
        >
          Reset
        </Button>
        <Input
          name={name}
          type={type}
          value={value}
          {...inputProps}
          onBlur={(e) => {
            const { target } = e;

            const fieldError = schema.safeParse(target.value).error;

            if (fieldError) {
              return setValidationMessage(fieldError.issues[0].message);
            }

            setValidationMessage("");
          }}
          onChange={(_, { value }) => {
            setValue(value);
            handleChange(value);
          }}
        />
      </div>
    </Field>
  );
}

export function TextFieldUncontrolled({
  label,
  description,
  hint,
  name,
  defaultValue,
  type,
  validationMessage,
  inputProps,
}: {
  label: string;
  description: React.ReactNode;
  hint?: string;
  name: string;
  defaultValue: string;
  type: InputProps["type"];
  inputProps?: InputProps;
  validationMessage?: string;
}) {
  const [value, setValue] = useState(defaultValue);

  const showHint = !validationMessage;

  return (
    <Field
      hint={showHint ? hint : undefined}
      // @ts-expect-error: label takes both string and JSX.element -
      // remove when FluentUI components package is updated to reflect that
      label={{
        children: (_: unknown, slotProps: LabelProps) => (
          <div>
            <Label {...slotProps}>{label}</Label>
            {description && (
              <p className={styles["field-description"]}>
                <Body1>{description}</Body1>
              </p>
            )}
          </div>
        ),
      }}
      {...(validationMessage && {
        validationState: "error",
        validationMessage,
      })}
      orientation="horizontal"
      className={styles.field}
    >
      <ResettableInput
        hidden={value == defaultValue}
        onReset={() => {
          const value = String(defaultValue);

          setValue(value);
        }}
      >
        Reset
        <Input
          name={name}
          type={type}
          value={value}
          {...inputProps}
          onChange={(_, { value }) => {
            setValue(value);
          }}
        />
      </ResettableInput>
    </Field>
  );
}

export function ResettableInput({
  hidden,
  onReset,
  children,
}: {
  hidden: boolean;
  onReset: () => void;
  children: React.ReactNode;
}) {
  return (
    <div className={styles["reset-input"]}>
      <Button
        appearance="subtle"
        icon={<ArrowResetRegular />}
        data-hidden={hidden}
        onClick={onReset}
      >
        Reset
      </Button>
      {children}
    </div>
  );
}
