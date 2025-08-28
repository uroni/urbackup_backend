import { useState } from "react";
import { Button } from "@fluentui/react-components";
import type { ZodMiniType } from "zod/v4-mini";

import type {
  GeneralSettings,
  SettingState,
} from "../../../api/urbackupserver";
import { CheckboxField } from "../Form/CheckboxField";
import {
  AdvancedServerFieldNames,
  advancedServerFields,
  advancedServerFormSchema,
  ServerFieldNames,
  serverFields,
  serverFormSchema,
} from "./serverForm";
import { TextField } from "../Form/TextField";
import { useSettings } from "../useSettings";
import { Form, FormCard, FormContainer } from "../Form/Form";
import { Field } from "../Form/types";

export function SettingsServer() {
  const { settings, updateSettings } = useSettings();

  const [initialFormState] = useState(() =>
    getInitialFormState([...serverFields, ...advancedServerFields], settings),
  );

  const [showAdvancedSettings, setShowAdvancedSettings] = useState(false);

  if (!settings) {
    return null;
  }

  return (
    <FormContainer>
      <h1>Server</h1>
      <FormCard>
        <FormSection
          fields={serverFields}
          schema={serverFormSchema}
          initialFormState={initialFormState}
          updateSettings={updateSettings}
        />
      </FormCard>

      {!showAdvancedSettings && (
        <div>
          <Button onClick={() => setShowAdvancedSettings(true)}>
            Show advanced settings
          </Button>
        </div>
      )}

      {showAdvancedSettings && (
        <>
          <h2>Advanced</h2>
          <FormCard>
            <FormSection
              fields={advancedServerFields}
              schema={advancedServerFormSchema}
              initialFormState={initialFormState}
              updateSettings={updateSettings}
            />
          </FormCard>
        </>
      )}
    </FormContainer>
  );
}

function FormSection<T extends string>({
  fields,
  schema,
  initialFormState,
  updateSettings,
}: {
  fields: Field<T>[];
  schema: Record<string, ZodMiniType>;
  initialFormState: Record<string, SettingState["value"]>;
  updateSettings: (
    settings: Omit<GeneralSettings["settings"], "can_edit_scripts">,
  ) => void;
}) {
  return (
    <Form>
      {fields.map((f) => {
        const initialValue = initialFormState[f.name];

        if (f.type === "checkbox") {
          return (
            <CheckboxField
              key={f.name}
              id={f.name}
              label={f.label}
              defaultChecked={!!initialValue}
              name={f.name}
              onChange={(data: boolean) => {
                updateSettings({
                  [f.name]: data,
                });
              }}
            />
          );
        }

        const value = f.transformer?.ui(+initialValue) ?? initialValue;

        return (
          <TextField
            key={f.name}
            label={f.label}
            description={f.description}
            hint={f.hint}
            name={f.name}
            defaultValue={String(value)}
            schema={schema[f.name]}
            type={f.type}
            onChange={(data) => {
              const newSetting = {
                [f.name]: f.transformer?.api(Number(data)) ?? data,
              };

              updateSettings(newSetting);
            }}
            inputProps={f.inputProps}
          />
        );
      })}
    </Form>
  );
}

function getInitialFormState(
  fields: Field<ServerFieldNames | AdvancedServerFieldNames>[],
  settings: Record<string, SettingState["value"]>,
) {
  return fields.reduce(
    (all, f) => ({ ...all, [f.name]: settings[f.name] ?? "" }),
    {} as Record<ServerFieldNames, SettingState["value"]>,
  );
}
