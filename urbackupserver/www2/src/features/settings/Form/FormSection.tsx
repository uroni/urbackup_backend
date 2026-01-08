import { Button } from "@fluentui/react-components";
import { useState } from "react";
import type { ZodMiniObject } from "zod/v4-mini";
import type { $ZodIssue } from "zod/v4/core";

import type { SettingState } from "../../../api/urbackupserver";
import { clearMessages } from "../../../components/Banner/messageStore";
import { CheckboxFieldUncontrolled } from "./CheckboxField";
import { Form } from "./Form";
import { TextFieldUncontrolled } from "./TextField";
import { Field } from "./types";

export function FormSection<T extends string, Settings>({
  fields,
  schema,
  initialFormState,
  onSubmit,
  children,
  button,
}: {
  fields: Field<T>[];
  schema: ZodMiniObject;
  initialFormState: Record<string, SettingState["value"]>;
  onSubmit: (settings: Settings) => void;
  children?: (validationMessages: Record<string, string>) => React.ReactNode;
  button?: React.ReactNode;
}) {
  const [validationMessages, setValidationMessages] = useState<
    Record<string, string>
  >(() => createBaseFormEntries(schema));

  const resetValidationMessages = () => {
    setValidationMessages(createBaseFormEntries(schema));
  };

  const handleSubmit = (ev: React.FormEvent<HTMLFormElement>) => {
    ev.preventDefault();

    const formData = new FormData(ev.currentTarget);

    const entries = {
      ...createBaseFormEntries(schema), // Get checkbox values if false
      ...Object.fromEntries(formData),
    };

    const parsed = schema.safeParse(entries);

    if (!parsed.success) {
      const newValidationMessages = formatErrorMessages(parsed.error.issues);
      setValidationMessages(newValidationMessages);

      clearMessages();

      return;
    }

    resetValidationMessages();

    const transformedData = transformForSubmission(fields, parsed.data);

    onSubmit(transformedData as Settings);
  };

  return (
    <Form onSubmit={handleSubmit} noValidate>
      {children?.(validationMessages)}
      {fields.map((f) => {
        const initialValue = initialFormState[f.name];

        if (f.type === "checkbox") {
          return (
            <CheckboxFieldUncontrolled
              key={f.name}
              id={f.name}
              label={f.label}
              defaultChecked={String(initialValue) === "true"}
              name={f.name}
            />
          );
        }

        const value = f.transformer?.ui(+initialValue) ?? initialValue;

        return (
          <TextFieldUncontrolled
            key={f.name}
            label={f.label}
            description={f.description}
            hint={f.hint}
            name={f.name}
            defaultValue={String(value)}
            validationMessage={validationMessages[f.name]}
            type={f.type}
            inputProps={f.inputProps}
          />
        );
      })}

      {button ?? (
        <Button type="submit" appearance="primary">
          Save settings
        </Button>
      )}
    </Form>
  );
}

function formatErrorMessages(issues: $ZodIssue[]) {
  return issues.reduce((all, i) => ({ ...all, [i.path[0]]: i.message }), {});
}

/**
 * Create object for extracting key/value from Zod schema
 */
function createBaseFormEntries(schema: ZodMiniObject) {
  const keys = Object.entries(schema.def.shape);

  const result = keys.reduce((all, [k]) => ({ ...all, [k]: "" }), {});

  return result;
}

function transformForSubmission<T extends string>(
  fields: Field<T>[],
  data: Record<string, any>,
) {
  const entries = Object.entries(data).map(([k, v]) => {
    const matchInFields = fields.find((f) => f.name === k);

    if (!matchInFields || !matchInFields.transformer) {
      return [k, v];
    }

    return [k, matchInFields.transformer.api(v)];
  });

  const result = Object.fromEntries(entries);

  return result;
}
