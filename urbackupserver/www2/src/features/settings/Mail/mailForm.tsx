import { z } from "zod/v4-mini";

import { integerValidation, VALIDATION_MESSAGES } from "../Form/validation";
import { type MailSettingsVals } from "../../../api/urbackupserver";
import { BaseField, Field } from "../Form/types";

export type MailSettingsKey = keyof MailSettingsVals;

export const MAIL_LABELS: Record<MailSettingsKey, string> = {
  mail_servername: "Mail server name",
  mail_serverport: "Mail server port",
  mail_username: "Mail server username (empty for none)",
  mail_password: "Mail server password",
  mail_from: "Sender E-Mail Address",
  mail_ssl_only: "Send mails only with SSL/TLS",
  mail_check_certificate: "Check SSL/TLS certificate",
  mail_use_smtps:
    "Use SSL encrypted SMTP (SMTPS) instead of SMTP with STARTTLS",
  mail_admin_addrs: "Server admin mail address",
};

function getLabelFromName(name: string, labels: Record<string, string>) {
  if (!Object.keys(labels).includes(name)) {
    return "";
  }

  return labels[name as keyof typeof labels];
}

const FORM_MESSAGES: Record<
  Extract<MailSettingsKey, "mail_serverport">,
  string
> = {
  mail_serverport: VALIDATION_MESSAGES.numeric(MAIL_LABELS["mail_serverport"]),
};

const stringBoolValidation = z.stringbool({
  falsy: [""],
});

export const mailFormSchema = z.object({
  mail_servername: z.string(),
  mail_serverport: integerValidation(FORM_MESSAGES["mail_serverport"]),
  mail_username: z.string(),
  mail_password: z.string(),
  mail_from: z.string(),
  mail_ssl_only: stringBoolValidation,
  mail_check_certificate: stringBoolValidation,
  mail_use_smtps: stringBoolValidation,
  mail_admin_addrs: z.string(),
});

const baseMailFields: BaseField<MailSettingsKey>[] = [
  {
    name: "mail_servername",
    type: "text",
  },
  {
    name: "mail_serverport",
    type: "text",
    inputProps: {
      "data-field-width": "33%",
    },
  },
  {
    name: "mail_username",
    type: "text",
  },
  {
    name: "mail_password",
    type: "password",
  },
  {
    name: "mail_from",
    type: "email",
  },
  {
    name: "mail_ssl_only",
    type: "checkbox",
  },
  {
    name: "mail_check_certificate",
    type: "checkbox",
  },
  {
    name: "mail_use_smtps",
    type: "checkbox",
  },
  {
    name: "mail_admin_addrs",
    type: "text",
    description:
      "List of email addresses to send mails to (comma/semicolon separated list).",
  },
];

export const mailFields = baseMailFields.map<Field<MailSettingsKey>>((f) => ({
  ...f,
  label: getLabelFromName(f.name, MAIL_LABELS),
}));
