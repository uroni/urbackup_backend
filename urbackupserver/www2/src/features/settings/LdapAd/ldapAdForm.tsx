import { z } from "zod/v4-mini";

import { integerValidation, VALIDATION_MESSAGES } from "../Form/validation";
import type { LdapSettingsVals } from "../../../api/urbackupserver";
import type { BaseField, Field } from "../Form/types";

export type LdapSettingsKey = keyof LdapSettingsVals;

export const LDAP_LABELS: Record<LdapSettingsKey, string> = {
  ldap_login_enabled: "Enable logins via LDAP/AD",
  ldap_server_name: "LDAP/AD server name",
  ldap_server_port: "LDAP/AD server port",
  ldap_username_prefix: "LDAP/AD user name prefix",
  ldap_username_suffix: "LDAP/AD user name suffix",
  ldap_group_class_query: "LDAP/AD group and class query",
  ldap_group_key_name: "LDAP/AD group key name in query",
  ldap_class_key_name: "LDAP/AD class key name in query",
  ldap_group_rights_map: "LDAP/AD group rights map",
  ldap_class_rights_map: "LDAP/AD class rights map",
};

const FORM_MESSAGES: Record<
  Extract<LdapSettingsKey, "ldap_server_port">,
  string
> = {
  ldap_server_port: VALIDATION_MESSAGES.numeric(
    LDAP_LABELS["ldap_server_port"],
  ),
};

const stringBoolValidation = z.stringbool({
  falsy: [""],
});

export const ldapAdFormSchema = z.object({
  ldap_login_enabled: stringBoolValidation,
  ldap_server_name: z.string(),
  ldap_server_port: integerValidation(FORM_MESSAGES["ldap_server_port"]),
  ldap_username_prefix: z.string(),
  ldap_username_suffix: z.string(),
  ldap_group_class_query: z.string(),
  ldap_group_key_name: z.string(),
  ldap_class_key_name: z.string(),
  ldap_group_rights_map: z.string(),
  ldap_class_rights_map: z.string(),
});

const baseLdapFields: BaseField<LdapSettingsKey>[] = [
  {
    name: "ldap_login_enabled",
    type: "checkbox",
  },
  {
    name: "ldap_server_name",
    type: "text",
  },
  {
    name: "ldap_server_port",
    type: "text",
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "ldap_username_prefix",
    type: "text",
  },
  {
    name: "ldap_username_suffix",
    type: "text",
  },
  {
    name: "ldap_group_class_query",
    type: "text",
    inputProps: {
      "data-field-width": "max",
    },
  },
  {
    name: "ldap_group_key_name",
    type: "text",
  },
  {
    name: "ldap_class_key_name",
    type: "text",
  },
  {
    name: "ldap_group_rights_map",
    type: "text",
    inputProps: {
      "data-field-width": "max",
    },
  },
  {
    name: "ldap_class_rights_map",
    type: "text",
    inputProps: {
      "data-field-width": "max",
    },
  },
];

export const ldapFields = createFieldsWithLabel<LdapSettingsKey>(
  baseLdapFields,
  LDAP_LABELS,
);

function createFieldsWithLabel<T extends string>(
  baseFields: BaseField<T>[],
  labels: Record<T, string>,
) {
  const result = baseFields.map<Field<T>>((f) => ({
    ...f,
    label: getLabelFromName(f.name, labels),
  }));

  return result;
}

function getLabelFromName(name: string, labels: Record<string, string>) {
  if (!Object.keys(labels).includes(name)) {
    return "";
  }

  return labels[name as keyof typeof labels];
}
