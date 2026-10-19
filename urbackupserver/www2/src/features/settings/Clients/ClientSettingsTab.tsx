import { Body1, Body2, Subtitle2, Title3 } from "@fluentui/react-components";
import { useParams } from "react-router-dom";
import { z, type ZodMiniType } from "zod/v4-mini";

import type { ClientSettings } from "../../../api/urbackupserver";
import { Banner } from "../../../components/Banner/Banner";
import { FormCard, FormContainer } from "../Form/Form";
import { FormSection } from "../Form/FormSection";
import type { BaseField } from "../Form/types";
import { VALIDATION_MESSAGES } from "../Form/validation";
import { useSettings } from "../useSettings";
import styles from "./Clients.module.css";
import {
  CheckboxDropdown,
  type InitialFormState as CheckboxDropdownState,
} from "./FileImageBackups/CheckboxDropdown";
import {
  createDropdownLabel,
  getSettingState,
} from "./FileImageBackups/FileImageBackups";
import { useFileBackups } from "./FileImageBackups/useFileBackups";
import {
  CheckboxTextDropdown,
  type InitialFormState as CheckboxTextDropdownState,
} from "./FileImageBackups/CheckboxTextDropdown";
import { MergeableFields } from "./FileImageBackups/MergeableFields";
import secondaryFormStyles from "./FileImageBackups/FileImageBackups.module.css";

/**
 * A client setting with a settings source selector
 */
export interface SettingField
  extends Pick<
    BaseField<string>,
    "description" | "inputProps" | "transformer"
  > {
  name: string;
  label: string;
  // "mergeable" settings can combine the values of several sources
  kind: "checkbox" | "text" | "mergeable";
  // Format of the value saved for "text" settings
  pattern?: RegExp;
  // Only shown for main clients, not for virtual sub clients
  mainClientOnly?: boolean;
}

/**
 * Page of the client settings that shows `fields` in one form
 */
export function ClientSettingsTab({
  title,
  fields,
}: {
  title: string;
  fields: SettingField[];
}) {
  const { clientId = "" } = useParams();

  const { navitems } = useSettings();

  const client = navitems.clients.find((c) => c.id === +clientId);

  if (!client) {
    return <h1>No client found with ID: {clientId}</h1>;
  }

  return (
    <ClientSettingsForm
      key={clientId}
      clientId={+clientId}
      clientName={client.name}
      title={title}
      fields={fields}
    />
  );
}

function ClientSettingsForm({
  clientId,
  clientName,
  title,
  fields,
}: {
  clientId: number;
  clientName: string;
  title: string;
  fields: SettingField[];
}) {
  const { settings, handleSubmit } = useFileBackups(clientId);

  const shownFields = fields.filter(
    (f) => !f.mainClientOnly || settings.main_client !== false,
  );

  return (
    <FormContainer>
      <h1>
        <Body2 className={styles["client-name"]}>{clientName}</Body2>
        <Title3>{title}</Title3>
      </h1>

      <Banner className={styles["sticky-banner"]} />

      {shownFields.length === 0 ? (
        <Body1 as="p" block>
          These settings can only be changed on the main client.
        </Body1>
      ) : (
        <FormCard>
          <div className="flow">
            <FormSection
              // The fields below keep their own state and transform their values
              fields={[]}
              schema={createSchema(shownFields)}
              initialFormState={{}}
              onSubmit={handleSubmit}
            >
              {(validationMessages) =>
                shownFields.map((field) => (
                  <SettingInput
                    key={field.name}
                    field={field}
                    settings={settings}
                    validationMessage={validationMessages[field.name]}
                  />
                ))
              }
            </FormSection>
          </div>
        </FormCard>
      )}
    </FormContainer>
  );
}

function SettingInput({
  field,
  settings,
  validationMessage,
}: {
  field: SettingField;
  settings: ClientSettings["settings"];
  validationMessage?: string;
}) {
  const formField = { ...field, type: "custom" } satisfies BaseField<string> & {
    label: string;
  };

  if (field.kind === "text") {
    return (
      <CheckboxTextDropdown
        name={field.name}
        dropdownLabel={createDropdownLabel(field.label.toLowerCase())}
        field={formField}
        initialFormState={getSettingState<CheckboxTextDropdownState>(
          settings,
          field.name,
        )}
        validationMessage={validationMessage}
      />
    );
  }

  if (field.kind === "mergeable") {
    return (
      <div className="flow">
        <Subtitle2 as="h3" className={styles["subtitle"]}>
          {field.label}
        </Subtitle2>
        {field.description && (
          <Body1 as="p" block className={secondaryFormStyles.description}>
            {field.description}
          </Body1>
        )}
        <MergeableFields
          name={field.name}
          label={field.label.toLowerCase()}
          initialFormState={getSettingState(settings, field.name)}
        />
      </div>
    );
  }

  return (
    <CheckboxDropdown
      name={field.name}
      dropdownLabel={createDropdownLabel(field.label.toLowerCase())}
      field={formField}
      initialFormState={getSettingState<CheckboxDropdownState>(
        settings,
        field.name,
      )}
    />
  );
}

const SOURCES = z.enum(["1", "2", "4"]);
// Any combination of the group, here and client sources
const MERGED_SOURCES = z.enum(Array.from({ length: 8 }, (_, i) => String(i)));

function createSchema(fields: SettingField[]) {
  return z.object(
    Object.fromEntries(
      fields.flatMap((f): [string, ZodMiniType][] => {
        if (f.kind === "checkbox") {
          return [
            [f.name, z.stringbool()],
            [`${f.name}.use`, SOURCES],
          ];
        }

        if (f.kind === "mergeable") {
          return [
            [f.name, z.string()],
            [`${f.name}.use`, MERGED_SOURCES],
          ];
        }

        const value = f.pattern
          ? z
              .string()
              .check(z.regex(f.pattern, VALIDATION_MESSAGES.regex(f.label)))
          : z.string();

        return [
          [f.name, value],
          [`${f.name}.use`, SOURCES],
        ];
      }),
    ),
  );
}
