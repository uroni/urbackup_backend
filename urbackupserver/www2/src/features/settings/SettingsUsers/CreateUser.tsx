import { useState } from "react";
import { z } from "zod/v4-mini";
import type { $ZodIssue } from "zod/v4/core";
import {
  Dialog,
  DialogTrigger,
  DialogSurface,
  DialogTitle,
  DialogContent,
  DialogBody,
  DialogActions,
  Button,
  Input,
  Field,
  Select,
} from "@fluentui/react-components";

import styles from "./CreateUser.module.css";
import {
  requiredStringValidation,
  VALIDATION_MESSAGES,
} from "../Fields/validation";
import { DismissRegular } from "@fluentui/react-icons";
import { UserAlreadyExistsError, UserRight } from "../../../api/urbackupserver";
import {
  addMessage,
  clearMessages,
} from "../../../components/Banner/messageStore";
import { useUsers, type UserInput } from "./useUsers";

const ADMIN_RIGHTS: UserRight[] = [
  {
    domain: "all",
    right: "all",
  },
];

const ADMINISTRATOR = "Administrator";

const userSchema = z
  .object({
    username: requiredStringValidation(
      VALIDATION_MESSAGES.required("username"),
    ),
    password: requiredStringValidation(
      VALIDATION_MESSAGES.required("password"),
    ),
    repeatPassword: requiredStringValidation(
      VALIDATION_MESSAGES.required("repeat password"),
    ),
    rights: z.string(),
  })
  .check(
    z.refine((data) => data.password === data.repeatPassword, {
      message: "Passwords do not match",
      path: ["repeatPassword"],
      // @ts-expect-error: update package and check if ZodMini type error is fixed
      when(payload) {
        return payload.issues.every((iss: $ZodIssue) => {
          const firstPathEl = iss.path?.[0];
          return firstPathEl !== "password" && firstPathEl !== "repeatPassword";
        });
      },
    }),
  );

interface ValidationMessages {
  username: string;
  password: string;
  repeatPassword: string;
  rights: string;
}

const initialValidationMessages: ValidationMessages = {
  username: "",
  password: "",
  repeatPassword: "",
  rights: ADMINISTRATOR,
};

export const CreateUser = () => {
  const [validationMessages, setValidationMessages] = useState(
    initialValidationMessages,
  );

  const { createUser } = useUsers();

  const handleSuccess = () => {
    clearMessages();
    addMessage({
      intent: "success",
      text: "New user successfully added.",
    });

    setOpen(false);
  };

  const handleFailure = () => {
    clearMessages();
    addMessage({
      intent: "error",
      text: "Failed to create new user.",
    });

    setOpen(false);
  };

  const resetValidationMessages = () => {
    setValidationMessages(initialValidationMessages);
  };

  const submitUser = (user: UserInput) => {
    createUser(user, {
      onError: (e) => {
        if (e instanceof UserAlreadyExistsError) {
          setValidationMessages({
            ...initialValidationMessages,
            username: "User with this name already exists",
          });
          return;
        }

        handleFailure();
      },
      onSuccess: handleSuccess,
    });
  };

  const handleSubmit = (ev: React.FormEvent<HTMLFormElement>) => {
    ev.preventDefault();

    const formData = new FormData(ev.currentTarget);

    const parsed = userSchema.safeParse(Object.fromEntries(formData));

    if (!parsed.success) {
      const newValidationMessages = formatErrorMessages(parsed.error.issues);
      setValidationMessages(newValidationMessages);

      return;
    }

    resetValidationMessages();

    submitUser(transformParsedData(parsed.data));
  };

  const [open, setOpen] = useState(false);

  return (
    <Dialog
      open={open}
      onOpenChange={(event, data) => {
        if (data.open) {
          resetValidationMessages();
        }
        setOpen(data.open);
      }}
    >
      <DialogTrigger disableButtonEnhancement>
        <Button appearance="primary">Create user</Button>
      </DialogTrigger>
      <DialogSurface className={`${styles.dialog}`}>
        <form onSubmit={handleSubmit}>
          <DialogBody>
            <DialogTitle
              action={
                <DialogTrigger action="close">
                  <Button
                    appearance="subtle"
                    aria-label="close"
                    icon={<DismissRegular />}
                  />
                </DialogTrigger>
              }
            >
              Create user
            </DialogTitle>
            <DialogContent className={`${styles.form} flow `}>
              <Field
                label="Username"
                validationMessage={validationMessages["username"]}
              >
                <Input name="username" />
              </Field>
              <Field
                label="Password"
                validationMessage={validationMessages["password"]}
              >
                <Input name="password" type="password" />
              </Field>
              <Field
                label="Repeat password"
                validationMessage={validationMessages["repeatPassword"]}
              >
                <Input name="repeatPassword" type="password" />
              </Field>
              <Field label="Rights">
                <Select name="rights">
                  <option>Administrator</option>
                </Select>
              </Field>
            </DialogContent>
            <DialogActions className={`${styles.actions}`}>
              <DialogTrigger disableButtonEnhancement>
                <Button>Cancel</Button>
              </DialogTrigger>
              <Button type="submit" appearance="primary">
                Submit
              </Button>
            </DialogActions>
          </DialogBody>
        </form>
      </DialogSurface>
    </Dialog>
  );
};

function transformParsedData(parsedData: ValidationMessages): UserInput {
  const updated = updateRights(parsedData);

  return removeRepeatPassword(updated);
}

function updateRights(parsedData: ValidationMessages) {
  const { rights } = parsedData;

  if (rights !== ADMINISTRATOR) {
    return {
      ...parsedData,
      rights: [],
    };
  }

  return {
    ...parsedData,
    rights: ADMIN_RIGHTS,
  };
}

function removeRepeatPassword(
  parsedData: UserInput & {
    repeatPassword: string;
  },
): UserInput {
  const { repeatPassword, ...rest } = parsedData;

  return rest;
}

function formatErrorMessages(issues: $ZodIssue[]) {
  return issues.reduce(
    (all, i) => ({ ...all, [i.path[0]]: i.message }),
    {} as ValidationMessages,
  );
}
