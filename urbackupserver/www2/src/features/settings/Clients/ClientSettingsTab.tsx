import { Body1, Body2, Title3 } from "@fluentui/react-components";
import { useParams } from "react-router-dom";
import { z } from "zod/v4-mini";

import type { ClientSettings } from "../../../api/urbackupserver";
import { Banner } from "../../../components/Banner/Banner";
import { FormCard, FormContainer } from "../Form/Form";
import { FormSection } from "../Form/FormSection";
import type { BaseField } from "../Form/types";
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

/**
 * A client setting with a settings source selector
 */
export interface SettingField {
  name: string;
  label: string;
  kind: "checkbox";
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
              {() =>
                shownFields.map((field) => (
                  <SettingInput
                    key={field.name}
                    field={field}
                    settings={settings}
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
}: {
  field: SettingField;
  settings: ClientSettings["settings"];
}) {
  const formField = { ...field, type: "custom" } satisfies BaseField<string> & {
    label: string;
  };

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

function createSchema(fields: SettingField[]) {
  return z.object(
    Object.fromEntries(
      fields.flatMap((f) => [
        [f.name, z.stringbool()],
        [`${f.name}.use`, z.enum(["1", "2"])],
      ]),
    ),
  );
}
