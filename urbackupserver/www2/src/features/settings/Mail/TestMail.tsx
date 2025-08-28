import { z } from "zod/v4-mini";
import { useState } from "react";
import { Button, Field, Input, Label } from "@fluentui/react-components";

import {
  clearMessages,
  Message,
} from "../../../components/Banner/messageStore";
import styles from "./Mail.module.css";
import { SingleBanner } from "../../../components/Banner/Banner";
import { MailSettings } from "../../../api/urbackupserver";

const TESTMAIL = {
  LABEL: "Send test mail to this email address",
  SCHEMA: z.email("Please enter a valid email address to test mail"),
  ID: "testmailaddr",
};

export function TestMail({
  onSubmit,
}: {
  onSubmit: (
    testmailaddr: string,
    mutationOptions: {
      onError: (e: Error) => void;
      onSuccess: (data: MailSettings) => void;
    },
  ) => void;
}) {
  const [feedback, setFeedback] = useState<Omit<Message, "id"> | null>(null);

  const [validationMessage, setValidationMessage] = useState("");

  const resetValidationMessages = () => {
    setValidationMessage("");
  };

  const handleSubmit = (ev: React.FormEvent<HTMLFormElement>) => {
    ev.preventDefault();

    const formData = new FormData(ev.currentTarget);

    const parsed = TESTMAIL.SCHEMA.safeParse(formData.get(TESTMAIL.ID));

    if (!parsed.success) {
      const newValidationMessage = parsed.error.issues[0].message;
      setValidationMessage(newValidationMessage);

      clearMessages();

      return;
    }

    resetValidationMessages();

    onSubmit(parsed.data, {
      onError: (e) => {
        setFeedback({
          intent: "error",
          text: e.message,
          title: "Sending test mail failed.",
        });
      },
      onSuccess: (data: MailSettings) => {
        if (data?.mail_test !== "ok") {
          throw Error(data.mail_test);
        }
        setFeedback({
          intent: "success",
          text: "Test mail sent successfully",
        });
      },
    });
  };

  return (
    <section className="flow">
      {feedback && (
        <SingleBanner message={feedback} onClick={() => setFeedback(null)} />
      )}
      <div className={styles.card}>
        <div className="flow flow-xs">
          <Label htmlFor={TESTMAIL.ID}>{TESTMAIL.LABEL}</Label>
          <form className={styles["single-field"]} onSubmit={handleSubmit}>
            <Field
              {...(validationMessage && {
                validationState: "error",
                validationMessage,
              })}
              className={styles.field}
            >
              <Input id={TESTMAIL.ID} name={TESTMAIL.ID} type="email" />
            </Field>
            <Button type="submit">Send test mail</Button>
          </form>
        </div>
      </div>
    </section>
  );
}
