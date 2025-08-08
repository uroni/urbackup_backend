import { FormEvent, useState } from "react";
import { urbackupServer } from "../App";
import { Field, MessageBar, MessageBarBody } from "@fluentui/react-components";
import { Button, Input, Spinner } from "@fluentui/react-components";
import {
  useMutation,
  useQueryClient,
  useSuspenseQuery,
} from "@tanstack/react-query";
import {
  LoginResult,
  PasswordWrongError,
  SessionNotFoundError,
  UsernameNotFoundError,
  UsernameOrPasswordWrongError,
} from "../api/urbackupserver";
import { Trans, t } from "@lingui/macro";
import { Navigate, useLocation, useNavigate } from "react-router-dom";

export function getSessionFromLocalStorage(): string {
  if (!window.localStorage) return "";
  return localStorage.getItem("ses") ?? "";
}

export function saveSessionToLocalStorage(session: string) {
  if (!window.localStorage) return;
  localStorage.setItem("ses", session);
}

async function handleLogin(session?: string) {
  if (session) {
    saveSessionToLocalStorage(session);
  }
}

export const useUser = () => {
  const queryClient = useQueryClient();

  const { data } = useSuspenseQuery({
    queryKey: ["user"],
    queryFn: async (): Promise<Partial<LoginResult> | null> => {
      try {
        const session = getSessionFromLocalStorage();
        console.log(session);

        if (session) {
          // Check if session is valid by making a request.
          // This will throw SessionNotFoundError if you need to login as a user.
          // Anon login will not work if a user exists.
          await urbackupServer.getUserList();

          return {
            session,
          };
        }

        // Do anonymouse login if no session saved
        const anonLogin = await urbackupServer.anonymousLogin();

        if (!anonLogin.success) {
          return null;
        }
        return anonLogin;
      } catch (error) {
        if (error instanceof SessionNotFoundError) {
          saveSessionToLocalStorage("");
        }
        return null;
      }
    },
  });

  const login = useMutation({
    mutationFn: async ({
      username,
      password,
      ldapLogin,
    }: {
      username: string;
      password: string;
      ldapLogin: boolean;
    }) => {
      const loginRes = await urbackupServer.login(
        username,
        password,
        ldapLogin,
      );
      return loginRes;
    },
    onSuccess: async (data) => {
      await handleLogin(data.session);
      return queryClient.invalidateQueries({
        queryKey: ["user"],
      });
    },
  });

  return {
    user: data,
    session: data?.session,
    login,
  };
};

const Login = () => {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [isLoading, setLoading] = useState(false);
  const [usernameValidationMessage, setUsernameValidationMessage] =
    useState("");
  const [passwordValidationMessage, setPasswordValidationMessage] =
    useState("");
  const [loginValidationMessage, setLoginValidationMessage] = useState("");

  const navigate = useNavigate();
  const { state: locationState } = useLocation();

  const { user, login } = useUser();

  const resetFormValidationMessages = () => {
    setUsernameValidationMessage("");
    setPasswordValidationMessage("");
    setLoginValidationMessage("");
  };

  const handleSubmitInt = async () => {
    const initres = user;
    if (typeof initres == "undefined") throw TypeError;

    if (!username) {
      setUsernameValidationMessage("Username is empty");
      return;
    }

    login.mutate(
      { username, password, ldapLogin: user?.ldap_enabled ?? false },
      {
        onError: (error) => {
          resetFormValidationMessages();
          if (error instanceof UsernameNotFoundError) {
            setUsernameValidationMessage("User not found on server");
          } else if (error instanceof UsernameOrPasswordWrongError) {
            setLoginValidationMessage("Incorrect username or password");
          } else if (error instanceof PasswordWrongError) {
            setPasswordValidationMessage("Password wrong");
          } else {
            throw error;
          }
        },
        onSuccess: async () => {
          await navigate(locationState?.pathname ?? "/status");
        },
      },
    );
  };

  const handleSubmit = async (e: FormEvent<HTMLFormElement>) => {
    e.preventDefault();
    setLoading(true);
    try {
      await handleSubmitInt();
    } finally {
      setLoading(false);
    }
  };

  if (user) {
    return <Navigate to={locationState?.pathname ?? "/"} replace />;
  }

  return (
    <div
      className="flow"
      style={{
        display: "grid",
        placeContent: "center",
        minHeight: "100vh",
        height: "100%",
      }}
    >
      <div className="flow">
        <h1>
          <Trans>Login</Trans>
        </h1>
        {loginValidationMessage && (
          <MessageBar intent="error">
            <MessageBarBody>{loginValidationMessage}</MessageBarBody>
          </MessageBar>
        )}
      </div>
      <form
        onSubmit={handleSubmit}
        className="flow flow-xl"
        style={{
          width: "32ch",
        }}
      >
        <Field
          label={t`Username`}
          required
          validationMessage={usernameValidationMessage}
        >
          <Input
            id="username"
            value={username}
            onChange={(e) => {
              setUsername(e.target.value);
            }}
          />
        </Field>
        <Field
          label={t`Password`}
          required
          validationMessage={passwordValidationMessage}
        >
          <Input
            id="password"
            type="password"
            value={password}
            onChange={(e) => {
              setPassword(e.target.value);
            }}
          />
        </Field>
        <div>
          {isLoading && <Spinner label={t`Logging in...`} />}
          {!isLoading && (
            <Button appearance="primary" type="submit">
              Log in
            </Button>
          )}
        </div>
      </form>
    </div>
  );
};

export default Login;
