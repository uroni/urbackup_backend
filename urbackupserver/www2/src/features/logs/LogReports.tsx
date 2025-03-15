import {
  Select,
  makeStyles,
  tokens,
  Field,
  Button,
  Textarea,
  Link,
  mergeClasses,
  Caption1,
} from "@fluentui/react-components";
import { useState } from "react";
import { useMutation, useSuspenseQuery } from "@tanstack/react-query";

import { urbackupServer } from "../../App";
import { LOG_LEVELS, SendOnly } from "../../api/urbackupserver";

const useStyles = makeStyles({
  actions: {
    "--flow-space": tokens.spacingVerticalXL,
  },
});

const initialFormState = {
  report_sendonly: false,
  report_loglevel: false,
  report_mail: false,
};

function useSaveLogReportingMutation() {
  return useMutation({
    mutationFn: ({
      mails,
      logLevel,
      sendOnly,
    }: {
      mails: string[];
      logLevel: (typeof LOG_LEVELS)[keyof typeof LOG_LEVELS];
      sendOnly: SendOnly;
    }) => urbackupServer.saveLogReporting(mails, logLevel, sendOnly),
  });
}

export function LogReports() {
  const logsResult = useSuspenseQuery({
    queryKey: ["logs"],
    queryFn: () => urbackupServer.getLogs([], LOG_LEVELS.INFO),
  });

  const {
    has_user,
    report_loglevel,
    report_sendonly,
    report_mail,
    can_report_script_edit,
  } = logsResult.data;

  const saveLogReportingMutation = useSaveLogReportingMutation();

  const [emailErrorMessage, setEmailErrorMessage] = useState("");

  const [hasUnsavedChanges, setHasUnsavedChanges] = useState(initialFormState);

  const classes = useStyles();

  if (!has_user) {
    return <div>You need to create a user to send reports</div>;
  }

  const handleSubmit = (e: React.FormEvent<HTMLFormElement>) => {
    e.preventDefault();

    const formData = new FormData(e.target as HTMLFormElement);

    const newMails = formData.get("report_new_mail");

    // Retrieve each email separated by new line
    const mails = newMails
      ?.toString()
      .trim()
      .split("\n")
      .filter((e) => e.length);

    // Verify emails are valid
    const invalidEmails = mails?.length ? checkEmailValidity(mails) : [];

    if (invalidEmails.length) {
      setEmailErrorMessage(`Invalid email(s): [${invalidEmails.join(", ")}]`);
      return;
    }

    const logLevel = formData.get("report_loglevel")!;
    const sendOnly = formData.get("report_sendonly")!;

    setEmailErrorMessage("");

    saveLogReportingMutation.mutate(
      {
        mails: mails ?? [],
        logLevel: +logLevel as (typeof LOG_LEVELS)[keyof typeof LOG_LEVELS],
        sendOnly: +sendOnly,
      },
      {
        onSuccess: () => {
          setHasUnsavedChanges(initialFormState);
        },
      },
    );
  };

  return (
    <form className="flow" onSubmit={handleSubmit}>
      <Field label="Send">
        <Select
          name="report_sendonly"
          id="report_sendonly"
          defaultValue={report_sendonly}
          onChange={(e) => {
            if (+e.target.value !== report_sendonly) {
              return setHasUnsavedChanges((p) => ({
                ...p,
                report_sendonly: true,
              }));
            }

            setHasUnsavedChanges((p) => ({
              ...p,
              report_sendonly: false,
            }));
          }}
        >
          <option value="0">All</option>
          <option value="1">Failed</option>
          <option value="3">Failed (not due to client timeout)</option>
          <option value="2">Successfull</option>
        </Select>
      </Field>
      <Field label="Minimum log level">
        <Select
          name="report_loglevel"
          id="report_loglevel"
          defaultValue={report_loglevel}
          onChange={(e) => {
            if (+e.target.value !== report_loglevel) {
              return setHasUnsavedChanges((p) => ({
                ...p,
                report_loglevel: true,
              }));
            }

            setHasUnsavedChanges((p) => ({
              ...p,
              report_loglevel: false,
            }));
          }}
        >
          <option value="0">Info</option>
          <option value="1">Warning</option>
          <option value="2">Error</option>
        </Select>
      </Field>
      <Field
        label="Send reports to (one email per line)"
        validationMessage={emailErrorMessage}
      >
        <Textarea
          name="report_new_mail"
          id="report_new_mail"
          defaultValue={`${report_mail.join("\n")}`}
          placeholder="email@example.com"
          autoComplete="off"
          spellCheck="false"
          rows={6}
          onChange={(e) => {
            if (e.target.value !== report_mail.join("\n")) {
              return setHasUnsavedChanges((p) => ({
                ...p,
                report_mail: true,
              }));
            }

            return setHasUnsavedChanges((p) => ({
              ...p,
              report_mail: false,
            }));
          }}
        />
      </Field>
      <div className={mergeClasses(classes.actions, "flow")}>
        <div className="cluster">
          <Button type="submit">Save Settings</Button>
          {Object.values(hasUnsavedChanges).some(Boolean) && (
            <Caption1>Unsaved changes</Caption1>
          )}
        </div>
        {can_report_script_edit && (
          <div>
            <Link>Edit report script</Link>
          </div>
        )}
      </div>
    </form>
  );
}

function checkEmailValidity(emails: string[]) {
  const invalidEmails = emails
    .map((e) => {
      const input = document.createElement("input");

      input.type = "email";
      input.required = true;
      input.value = e;

      if (!input.checkValidity()) {
        return e;
      }

      return "";
    })
    .filter((e) => e.length);

  return invalidEmails;
}
