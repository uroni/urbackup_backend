import * as React from "react";
import { Suspense, useEffect, useState } from "react";
import HeaderBar from "./components/HeaderBar";
import NavSidebar from "./components/NavSidebar";
import { proxy, useSnapshot } from "valtio";
import { createHashRouter, RouterProvider } from "react-router-dom";
import LoginPage from "./pages/Login";
import StatusPage from "./pages/Status";
import {
  FluentProvider,
  teamsLightTheme,
  teamsDarkTheme,
  Spinner,
  Toaster,
} from "@fluentui/react-components";
import { useStackStyles } from "./components/StackStyles";
import UrBackupServer, { SessionNotFoundError } from "./api/urbackupserver";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { i18n } from "@lingui/core";
import { I18nProvider } from "@lingui/react";

const initialDark =
  window.matchMedia &&
  window.matchMedia("(prefers-color-scheme: dark)").matches;
const initialTheme = initialDark ? teamsDarkTheme : teamsLightTheme;

export enum Pages {
  Status = "status",
  Activities = "activities",
  Login = "login",
  About = "about",
}

export const state = proxy({
  loggedIn: false,
  activePage: Pages.Status,
  pageAfterLogin: Pages.Status,
  startupComplete: false,
});

export const urbackupServer = new UrBackupServer(
  "x",
  getSessionFromLocalStorage(),
);

async function isLoggedIn(): Promise<boolean> {
  try {
    await urbackupServer.status();
  } catch (error) {
    if (error instanceof SessionNotFoundError) return false;
  }
  return true;
}

async function jumpToLoginPageIfNeccessary() {
  if (state.startupComplete && state.loggedIn) {
    state.activePage = state.pageAfterLogin;
    return;
  }

  if (await isLoggedIn()) {
    state.loggedIn = true;
    state.startupComplete = true;
    state.activePage = state.pageAfterLogin;
  } else {
    state.loggedIn = false;
    await router.navigate(`/`);
  }
}

export const router = createHashRouter([
  {
    path: "/",
    element: <LoginPage />,
    loader: async () => {
      if (await isLoggedIn()) {
        state.loggedIn = true;
        state.startupComplete = true;
        await router.navigate(`/${Pages.Status}`);
        return;
      }
      state.activePage = Pages.Login;
      state.startupComplete = true;
      state.loggedIn = false;
      return null;
    },
    errorElement: <div>Failed to log in.</div>,
  },
  {
    path: `/${Pages.Status}`,
    element: <StatusPage />,
    loader: async () => {
      state.pageAfterLogin = Pages.Status;
      await jumpToLoginPageIfNeccessary();
      return null;
    },
    errorElement: <div>Failed to fetch clients.</div>,
  },
  {
    path: "/about",
    element: <div>About page</div>,
  },
  {
    path: `/${Pages.Activities}`,
    element: <div>Activities page</div>,
    loader: async () => {
      state.pageAfterLogin = Pages.Activities;
      await jumpToLoginPageIfNeccessary();
      return null;
    },
  },
]);

function getSessionFromLocalStorage(): string {
  if (!window.localStorage) return "";
  return localStorage.getItem("ses") ?? "";
}

export function saveSessionToLocalStorage(session: string) {
  if (!window.localStorage) return;
  localStorage.setItem("ses", session);
}

const queryClient = new QueryClient();

export async function dynamicActivateTranslation(locale: string) {
  const { messages } = await import(`./locales/${locale}.po`);

  i18n.load(locale, messages);
  i18n.activate(locale);
}

const App: React.FunctionComponent = () => {
  const [selectedTheme, setTheme] = useState(initialTheme);

  const snap = useSnapshot(state);

  useEffect(() => {
    window
      .matchMedia("(prefers-color-scheme: dark)")
      .addEventListener("change", (event) => {
        setTheme(event.matches ? teamsDarkTheme : teamsLightTheme);
      });
    void (async () => {
      await dynamicActivateTranslation("en");
    })();
  }, []);

  const styles = useStackStyles();

  return (
    <FluentProvider theme={selectedTheme}>
      <React.StrictMode>
        <I18nProvider i18n={i18n}>
          <QueryClientProvider client={queryClient}>
            <div className={styles.stackVertical}>
              <div className={styles.item}>
                <HeaderBar />
              </div>
              <div
                className={styles.itemGrow}
                style={{
                  display: "flex",
                  flexDirection: "column",
                }}
              >
                <div
                  className={styles.stackHorizontal}
                  style={{
                    flex: 1,
                  }}
                >
                  {snap.loggedIn && (
                    <div
                      className={styles.item}
                      style={{
                        borderRight: "1px solid",
                        padding: "10pt",
                      }}
                    >
                      <NavSidebar />
                    </div>
                  )}
                  <div className={styles.itemGrow} style={{ padding: "10pt" }}>
                    <Suspense fallback={<Spinner />}>
                      <RouterProvider router={router} />
                    </Suspense>
                  </div>
                </div>
              </div>
            </div>
            <Toaster toasterId="toaster" />
          </QueryClientProvider>
        </I18nProvider>
      </React.StrictMode>
    </FluentProvider>
  );
};

export default App;
