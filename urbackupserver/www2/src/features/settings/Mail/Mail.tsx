import { useState } from "react";
import { Button } from "@fluentui/react-components";
import type { ZodMiniObject } from "zod/v4-mini";
import type { $ZodIssue } from "zod/v4/core";

import type {
  MailSettingsVals,
  SettingState,
  StringBoolSetting,
} from "../../../api/urbackupserver";
import { CheckboxFieldUncontrolled } from "../Form/CheckboxField";
import { mailFields, mailFormSchema, MailSettingsKey } from "./mailForm";
import { TextFieldUncontrolled } from "../Form/TextField";
import { Banner } from "../../../components/Banner/Banner";
import { clearMessages } from "../../../components/Banner/messageStore";
import { useMail } from "./useMail";
import { TestMail } from "./TestMail";
import { Field } from "../Form/types";
import { Form, FormCard, FormContainer } from "../Form/Form";

export function Mail() {
  const { settings, handleSubmit, sendTestMail } = useMail();

  const [initialFormState] = useState(() =>
    getInitialFormState(mailFields, settings),
  );

  if (!settings) {
    return null;
  }

  return (
    <FormContainer>
      <h1>Mail</h1>

      <Banner />

      <FormCard>
        <FormSection
          fields={mailFields}
          schema={mailFormSchema}
          initialFormState={initialFormState}
          onSubmit={handleSubmit}
        />
      </FormCard>

      <TestMail onSubmit={sendTestMail} />
    </FormContainer>
  );
}

function FormSection<T extends string>({
  fields,
  schema,
  initialFormState,
  onSubmit,
}: {
  fields: Field<T>[];
  schema: ZodMiniObject;
  initialFormState: Record<string, SettingState["value"]>;
  onSubmit: (settings: MailSettingsVals) => void;
}) {
  const [validationMessages, setValidationMessages] = useState<
    Record<string, string>
  >(() => createInitialValidationMessages(schema));

  const resetValidationMessages = () => {
    setValidationMessages(createInitialValidationMessages(schema));
  };

  const handleSubmit = (ev: React.FormEvent<HTMLFormElement>) => {
    ev.preventDefault();

    const formData = new FormData(ev.currentTarget);

    const entries = {
      ...createInitialValidationMessages(schema),
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

    onSubmit(parsed.data as MailSettingsVals);
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
              defaultChecked={!!initialValue}
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
        Save mail settings
      </Button>
    </Form>
  );
}

function getInitialFormState<T>(
  fields: Field<MailSettingsKey>[],
  settings: MailSettingsVals,
) {
  return fields.reduce(
    (all, f) => ({ ...all, [f.name]: transformValue(settings[f.name]) }),
    {},
  );
}

function transformValue(value: StringBoolSetting | (string & {})) {
  if (value === "true" || value === "false") {
    return value === "true";
  }

  return value ?? "";
}

function formatErrorMessages(issues: $ZodIssue[]) {
  return issues.reduce((all, i) => ({ ...all, [i.path[0]]: i.message }), {});
}

/**
 * Create object for displaying validation messages from Zod schema
 * for non-Boolean types.
 */
function createInitialValidationMessages(schema: ZodMiniObject) {
  const nonBooleanKeys = Object.entries(schema.def.shape).filter(
    ([_, v]) => v._zod.def.type !== "boolean",
  );

  const result = nonBooleanKeys.reduce((all, [k]) => ({ ...all, [k]: "" }), {});

  return result;
}
