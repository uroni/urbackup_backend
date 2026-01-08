import { useMemo } from "react";
import {
  Body1,
  Body2,
  Button,
  Subtitle1,
  Subtitle2,
  Title3,
} from "@fluentui/react-components";
import { useParams } from "react-router-dom";

import { FormCard, FormContainer } from "../../Form/Form";
import { FormSection } from "../../Form/FormSection";
import {
  defaultDirsSchema,
  excludeFilesSchema,
  fileBackupsFields,
  fileBackupsFormSchema,
  FileBackupsSettingsKey,
  includeFilesSchema,
} from "./backupsForm";
import { useSettings } from "../../useSettings";
import styles from "../Clients.module.css";
import secondaryFormStyles from "./FileImageBackups.module.css";
import { useFileBackups } from "./useFileBackups";
import {
  ClientSettingState,
  SettingState,
} from "../../../../api/urbackupserver";
import { Field } from "../../Form/types";
import { CheckboxTextDropdown } from "./CheckboxTextDropdown";
import { MergeableFields } from "./MergeableFields";
import { Banner } from "../../../../components/Banner/Banner";
import { CheckboxDropdown } from "./CheckboxDropdown";
import { VALUE_TO_USE } from "./utils";

const checkboxFieldInputs = [
  {
    name: "update_freq_incr",
    labels: {
      checkbox: "Enable incremental file backups",
      dropdown: createDropdownLabel(
        "incremental file backup frequency interval",
      ),
    },
  },
  {
    name: "update_freq_full",
    labels: {
      checkbox: "Enable full file backups",
      dropdown: createDropdownLabel("full file backup frequency interval"),
    },
  },
  {
    name: "max_file_incr",
    labels: {
      dropdown: createDropdownLabel("max incremental file backups"),
    },
  },
  {
    name: "min_file_incr",
    labels: {
      dropdown: createDropdownLabel("min incremental file backups"),
    },
  },
  {
    name: "max_file_full",
    labels: {
      dropdown: createDropdownLabel("max full file backups"),
    },
  },
  {
    name: "min_file_full",
    labels: {
      dropdown: createDropdownLabel("min full file backups"),
    },
  },
];

export function FileImageBackups() {
  const { clientId } = useParams();

  const { navitems } = useSettings();

  const client = navitems.clients.find((c) => c.id === +clientId!);

  if (!client) {
    return <h1>No client found with ID: {clientId}</h1>;
  }

  const { settings, handleSubmit } = useFileBackups(+clientId!);

  const initialFormState = useMemo(
    () =>
      getInitialFormState<FileBackupsSettingsKey>(fileBackupsFields, settings),
    [settings, clientId],
  );

  return (
    <FormContainer key={clientId}>
      <h1>
        <Body2 className={styles["client-name"]}>{client?.name}</Body2>
        <Title3>Backups</Title3>
      </h1>

      <Subtitle1 as="h2" className={styles["subtitle"]}>
        File Backups
      </Subtitle1>

      <Banner className={styles["sticky-banner"]} />

      <FormCard>
        <div className="flow">
          <FormSection
            fields={fileBackupsFields.filter((f) => f.type !== "custom")}
            schema={fileBackupsFormSchema}
            initialFormState={initialFormState}
            onSubmit={handleSubmit}
          >
            {(validationMessages) => (
              <>
                {checkboxFieldInputs.map(({ name, labels }) => (
                  <CheckboxTextDropdown
                    key={name}
                    name={name}
                    checkboxLabel={labels?.checkbox}
                    dropdownLabel={labels.dropdown}
                    field={fileBackupsFields.find((f) => f.name === name)!}
                    initialFormState={settings[name]}
                    validationMessage={validationMessages[name]}
                  />
                ))}
                {[
                  {
                    field: "backup_dirs_optional",
                    labels: {
                      dropdown: createDropdownLabel(
                        "optional directories to backup",
                      ),
                    },
                  },
                ].map(({ field, labels }) => (
                  <CheckboxDropdown
                    key={field}
                    name={field}
                    dropdownLabel={labels.dropdown}
                    field={fileBackupsFields.find((f) => f.name === field)!}
                    initialFormState={settings[field]}
                  />
                ))}
              </>
            )}
          </FormSection>
        </div>
      </FormCard>

      {[
        {
          name: "exclude_files",
          label: "excluded files",
          schema: excludeFilesSchema,
        },
        {
          name: "include_files",
          label: "included files",
          schema: includeFilesSchema,
        },
        {
          name: "default_dirs",
          label: "default directories",
          schema: defaultDirsSchema,
        },
      ].map(({ name, label, schema }, i) => {
        const field = fileBackupsFields.find((f) => f.name === name);

        return (
          <div key={name} className={secondaryFormStyles["secondary-form"]}>
            <div className="flow">
              <div className="flow">
                <Subtitle2 as="h3" className={styles["subtitle"]}>
                  {field?.label}
                </Subtitle2>
                {field?.description && (
                  <Body1
                    as="p"
                    block
                    className={secondaryFormStyles.description}
                  >
                    {field.description}
                  </Body1>
                )}
              </div>
              <FormCard>
                <div className="flow">
                  <FormSection
                    fields={fileBackupsFields.filter(
                      (f) => f.type !== "custom",
                    )}
                    schema={schema}
                    initialFormState={initialFormState}
                    onSubmit={handleSubmit}
                    button={<Button type="submit">Save settings</Button>}
                  >
                    {() => (
                      <MergeableFields
                        key={name}
                        name={name}
                        label={label}
                        initialFormState={settings[name]}
                      />
                    )}
                  </FormSection>
                </div>
              </FormCard>
            </div>
          </div>
        );
      })}
    </FormContainer>
  );
}

function getInitialFormState<T extends string>(
  fields: Field<T>[],
  settings: Record<T, SettingState["value"]>,
) {
  return fields.reduce(
    (all, f) => ({
      ...all,
      [f.name]: transformValue(getValueFromSettings(f.name, settings)),
    }),
    {} as typeof settings,
  );
}

function transformValue(value: ClientSettingState) {
  if (value?.use) {
    return value[VALUE_TO_USE[value.use]];
  }

  return String(value) ?? "";
}

function getValueFromSettings(
  key: string,
  settings: Record<string, SettingState["value"]>,
): string | boolean | number {
  if (!key.includes(".") && key in settings) {
    return settings[key];
  }

  const [k, ...rest] = key.split(".");

  return getValueFromSettings(rest.join("."), settings[k]);
}

function createDropdownLabel(text: string) {
  return `Select settings source for ${text}`;
}
