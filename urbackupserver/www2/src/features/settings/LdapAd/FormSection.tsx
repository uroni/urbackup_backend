import { Button } from "@fluentui/react-components";
import { useState } from "react";
import type { ZodMiniObject } from "zod/v4-mini";
import type { $ZodIssue } from "zod/v4/core";
import type { SettingState } from "../../../api/urbackupserver";
import { clearMessages } from "../../../components/Banner/messageStore";
import { CheckboxFieldUncontrolled } from "../Form/CheckboxField";
import { Form } from "../Form/Form";
import { TextFieldUncontrolled } from "../Form/TextField";
import { Field } from "../Form/types";

export function FormSection<T extends string, Settings>({
  fields,
  schema,
  initialFormState,
  onSubmit,
}: {
  fields: Field<T>[];
  schema: ZodMiniObject;
  initialFormState: Record<string, SettingState["value"]>;
  onSubmit: (settings: Settings) => void;
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

    onSubmit(parsed.data as Settings);
  };

  return (
    <Form onSubmit={handleSubmit} noValidate>
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

      <Button type="submit" appearance="primary">
        Save settings
      </Button>
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
