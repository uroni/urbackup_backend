import { FormEvent, useEffect, useState } from "react";
import {
  router,
  saveSessionToLocalStorage,
  state,
  urbackupServer,
} from "../App";
import { Field } from "@fluentui/react-components";
import { Button, Input, Spinner } from "@fluentui/react-components";
import { useSuspenseQuery } from "@tanstack/react-query";
import {
  PasswordWrongError,
  UsernameNotFoundError,
  UsernameOrPasswordWrongError,
} from "../api/urbackupserver";
import { Trans, t } from "@lingui/macro";

const Login = () => {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [isLoading, setLoading] = useState(false);
  const [usernameValidationMessage, setUsernameValidationMessage] =
    useState("");
  const [passwordValidationMessage, setPasswordValidationMessage] =
    useState("");

  const anonymousLoginResult = useSuspenseQuery({
    queryKey: ["anonymousLogin"],
    queryFn: urbackupServer.anonymousLogin,
  });

  // Handle successful login
  useEffect(() => {
    if (anonymousLoginResult.data) {
      const data = anonymousLoginResult.data;

      async function handleLogin() {
        if (data.session) saveSessionToLocalStorage(data.session);

        if (data.success) {
          state.loggedIn = true;
          state.startupComplete = true;
          await router.navigate(`/${state.pageAfterLogin}`);
        }
      }

      void handleLogin();
    }
  }, [anonymousLoginResult.data]);

  const handleSubmitInt = async () => {
    const initres = anonymousLoginResult.data;
    if (typeof initres == "undefined") throw TypeError;

    if (!username) {
      setUsernameValidationMessage("Username is empty");
      return;
    }

    try {
      const loginRes = await urbackupServer.login(
        username,
        password,
        initres.ldap_enabled ?? false,
      );
      if (loginRes.session) saveSessionToLocalStorage(loginRes.session);
      if (loginRes.success) {
        state.startupComplete = true;
        await router.navigate(`/${state.pageAfterLogin}`);
        return;
      }
    } catch (e) {
      if (e instanceof UsernameNotFoundError) {
        setUsernameValidationMessage("User not found on server");
      } else if (e instanceof UsernameOrPasswordWrongError) {
        setUsernameValidationMessage(
          "Login with username and password combination failed",
        );
        setPasswordValidationMessage(
          "Login with username and password combination failed",
        );
      } else if (e instanceof PasswordWrongError) {
        setPasswordValidationMessage("Password wrong");
      } else {
        throw e;
      }
    }
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
  return (
    <div
      style={{
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        height: "100%",
      }}
    >
      <div>
        <h3>
          <Trans>Login:</Trans>
        </h3>
        <div>
          <form onSubmit={handleSubmit}>
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
            {isLoading && <Spinner label={t`Logging in...`} />}
            {!isLoading && <Button type="submit">Log in</Button>}
          </form>
        </div>
      </div>
    </div>
  );
};

export default Login;
