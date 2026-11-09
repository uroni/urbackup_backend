import { useState } from "react";
import { Button, Field, Input } from "@fluentui/react-components";

import { PasswordWrongError } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";
import { Banner } from "../../components/Banner/Banner";
import {
  addMessage,
  clearMessages,
} from "../../components/Banner/messageStore";
import { FormCard, FormContainer } from "./Form/Form";

/**
 * Changes the password of the logged in user
 */
export function ChangePassword() {
  const [passwords, setPasswords] = useState({ old: "", new: "", repeat: "" });
  const [errors, setErrors] = useState<Partial<typeof passwords>>({});

  const set = (key: keyof typeof passwords, value: string) => {
    setPasswords((p) => ({ ...p, [key]: value }));
    setErrors((e) => ({ ...e, [key]: undefined }));
  };

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();

    if (passwords.new === "") {
      return setErrors({ new: "Please enter a new password" });
    }

    if (passwords.new !== passwords.repeat) {
      return setErrors({ repeat: "The passwords differ" });
    }

    clearMessages();

    try {
      await urbackupServer.changeOwnPassword(passwords.old, passwords.new);
      setPasswords({ old: "", new: "", repeat: "" });
      addMessage({ intent: "success", text: "Changed the password." });
    } catch (error) {
      if (error instanceof PasswordWrongError) {
        return setErrors({ old: "The old password is wrong" });
      }

      addMessage({ intent: "error", text: "Changing the password failed." });
    }
  };

  return (
    <FormContainer>
      <h1>Change password</h1>

      <Banner />

      <FormCard>
        <form className="flow" onSubmit={submit}>
          {(
            [
              ["old", "Old password", "current-password"],
              ["new", "New password", "new-password"],
              ["repeat", "Repeat new password", "new-password"],
            ] as const
          ).map(([key, label, autoComplete]) => (
            <Field key={key} label={label} validationMessage={errors[key]}>
              <Input
                type="password"
                autoComplete={autoComplete}
                value={passwords[key]}
                onChange={(_, d) => set(key, d.value)}
              />
            </Field>
          ))}
          <div>
            <Button type="submit" appearance="primary">
              Change password
            </Button>
          </div>
        </form>
      </FormCard>
    </FormContainer>
  );
}
