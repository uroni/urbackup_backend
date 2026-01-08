import * as React from "react";
import { Suspense, useEffect, useState } from "react";
import { createHashRouter, RouterProvider } from "react-router-dom";
import LoginPage, { getSessionFromLocalStorage } from "./pages/Login";
import { StatusPage } from "./pages/Status";
import {
  FluentProvider,
  teamsLightTheme,
  teamsDarkTheme,
  Spinner,
  Toaster,
  Link,
} from "@fluentui/react-components";
import { ReactQueryDevtools } from "@tanstack/react-query-devtools";
import UrBackupServer from "./api/urbackupserver";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { i18n } from "@lingui/core";
import { I18nProvider } from "@lingui/react";
import { ErrorBoundary, ErrorPage } from "./components/ErrorPage";
import { Layout } from "./components/Layout";
import { AuthenticatedRoute } from "./components/AuthenticatedRoute";
import "./css/global.css";

const initialDark =
  window.matchMedia &&
  window.matchMedia("(prefers-color-scheme: dark)").matches;
const initialTheme = initialDark ? teamsDarkTheme : teamsLightTheme;

export enum Pages {
  Status = "status",
  Activities = "activities",
  Backups = "backups",
  Statistics = "statistics",
  Login = "login",
  About = "about",
  Logs = "logs",
  Settings = "settings",
}

// Not using any global state, at the moment
// export const state = proxy({});

export const urbackupServer = new UrBackupServer(
  "x",
  getSessionFromLocalStorage(),
);

export const router = createHashRouter([
  {
    path: "/login",
    element: <LoginPage />,
    errorElement: <div>Failed to log in.</div>,
  },
  {
    path: "/",
    element: (
      <AuthenticatedRoute>
        <Layout />
      </AuthenticatedRoute>
    ),
    ErrorBoundary,
    children: [
      {
        index: true,
        element: <StatusPage />,
      },
      {
        path: `/${Pages.Status}`,
        element: <StatusPage />,
      },
      {
        path: "/about",
        element: <div>About page</div>,
      },
      {
        path: `/${Pages.Activities}`,
        lazy: async () => {
          const { ActivitiesPage } = await import("./pages/Activities");
          return { Component: ActivitiesPage };
        },
      },
      {
        path: `/${Pages.Backups}`,
        lazy: async () => {
          const { BackupsPage } = await import("./pages/Backups");
          return { Component: BackupsPage };
        },
        errorElement: (
          <ErrorPage returnToLink={<Link href="/#/backups">Backups</Link>} />
        ),
        children: [
          {
            index: true,
            lazy: async () => {
              const { BackupsTable } = await import(
                "./features/backups/BackupsTable"
              );
              return { Component: BackupsTable };
            },
          },
          {
            path: ":clientId",
            lazy: async () => {
              const { ClientBackupsTable } = await import(
                "./features/backups/ClientBackupsTable"
              );
              return { Component: ClientBackupsTable };
            },
          },
          {
            path: ":clientId/:backupId",
            lazy: async () => {
              const { BackupContentTable } = await import(
                "./features/backups/BackupContentTable"
              );
              return { Component: BackupContentTable };
            },
          },
        ],
      },
      {
        path: `/${Pages.Statistics}`,
        lazy: async () => {
          const { StatisticsPage } = await import("./pages/Statistics");
          return { Component: StatisticsPage };
        },
      },
      {
        path: `/${Pages.Logs}`,
        lazy: async () => {
          const { LogsPage } = await import("./pages/Logs");
          return { Component: LogsPage };
        },
        errorElement: (
          <ErrorPage returnToLink={<Link href="/#/logs">Logs</Link>} />
        ),
        children: [
          {
            index: true,
            lazy: async () => {
              const { ClientLogs } = await import("./features/logs/ClientLogs");
              return { Component: ClientLogs };
            },
          },
          {
            path: ":logId",
            lazy: async () => {
              const { ClientLog } = await import("./features/logs/ClientLog");
              return { Component: ClientLog };
            },
          },
        ],
      },
      {
        path: `/${Pages.Settings}`,
        lazy: async () => {
          const { SettingsPage } = await import("./pages/Settings");
          return { Component: SettingsPage };
        },
        children: [
          {
            index: true,
            lazy: async () => {
              const { SettingsServer } = await import(
                "./features/settings/SettingsServer/SettingsServer"
              );
              return { Component: SettingsServer };
            },
          },
          {
            path: "server",
            lazy: async () => {
              const { SettingsServer } = await import(
                "./features/settings/SettingsServer/SettingsServer"
              );
              return { Component: SettingsServer };
            },
          },
          {
            path: "mail",
            lazy: async () => {
              const { Mail } = await import("./features/settings/Mail/Mail");
              return { Component: Mail };
            },
          },
          {
            path: "ldap-ad",
            lazy: async () => {
              const { LdapAd } = await import(
                "./features/settings/LdapAd/LdapAd"
              );
              return { Component: LdapAd };
            },
          },
          {
            path: "users",
            lazy: async () => {
              const { SettingsUsers } = await import(
                "./features/settings/SettingsUsers/SettingsUsers"
              );
              return { Component: SettingsUsers };
            },
          },
          {
            path: "clients",
            lazy: async () => {
              const { Clients } = await import(
                "./features/settings/Clients/Clients"
              );
              return { Component: Clients };
            },
            children: [
              {
                path: ":clientId",
                lazy: async () => {
                  const { FileImageBackups } = await import(
                    "./features/settings/Clients/FileImageBackups/FileImageBackups"
                  );
                  return { Component: FileImageBackups };
                },
              },
              {
                path: ":clientId/backups",
                lazy: async () => {
                  const { FileImageBackups } = await import(
                    "./features/settings/Clients/FileImageBackups/FileImageBackups"
                  );
                  return { Component: FileImageBackups };
                },
              },
              {
                path: ":clientId/permissions",
                lazy: async () => {
                  const { Permissions } = await import(
                    "./features/settings/Clients/Permissions"
                  );
                  return { Component: Permissions };
                },
              },
            ],
          },
        ],
      },
    ],
  },
]);

const queryClient = new QueryClient();

export async function dynamicActivateTranslation(locale: string) {
  const { messages } = await import(`./locales/${locale}.po`);

  i18n.load(locale, messages);
  i18n.activate(locale);
}

const App: React.FunctionComponent = () => {
  const [selectedTheme, setTheme] = useState(initialTheme);

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

  return (
    <FluentProvider theme={selectedTheme}>
      <React.StrictMode>
        <I18nProvider i18n={i18n}>
          <QueryClientProvider client={queryClient}>
            <Suspense
              fallback={
                <div
                  style={{
                    display: "grid",
                    placeContent: "center",
                    minHeight: "100vh",
                  }}
                >
                  <Spinner />
                </div>
              }
            >
              <RouterProvider router={router} />
            </Suspense>
            <Toaster toasterId="toaster" />
            {/* Following only bundled in development mode */}
            <ReactQueryDevtools initialIsOpen={false} />
          </QueryClientProvider>
        </I18nProvider>
      </React.StrictMode>
    </FluentProvider>
  );
};

export default App;
