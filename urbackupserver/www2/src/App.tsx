import * as React from 'react';
import { useEffect, useState } from 'react';
import HeaderBar from './components/HeaderBar';
import NavSidebar from './components/NavSidebar';
import { proxy, useSnapshot } from 'valtio';
import { createHashRouter, RouterProvider } from 'react-router-dom';
import LoginPage from './pages/Login';
import StatusPage from './pages/Status';
import { FluentProvider, teamsLightTheme, teamsDarkTheme, makeStyles } from '@fluentui/react-components';
import { useStackStyles } from './components/StackStyles';
import UrBackupServer from './api/urbackupserver';
import { QueryClient, QueryClientProvider } from 'react-query';

const initialDark = (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches);
const initialTheme =
  initialDark ? teamsDarkTheme : teamsLightTheme;

export enum Pages {
  Status = "status",
  Activities = "activities",
  Login = "login",
  About = "about"
}

export const state = proxy({
  loggedIn: false,
  activePage: Pages.Status
});


export const router = createHashRouter([
  {
    path: "/",
    element: (
      <LoginPage />
    ),
    loader: async () => {
      state.activePage = Pages.Login;
      return null;
    }
  },
  {
    path: `/${Pages.Status}`,
    element: (
      <StatusPage />
    ),
    loader: async () => {
      state.activePage = Pages.Status;
      return null;
    }
  },
  {
    path: "/about",
    element: <div>About page</div>
  },
  {
    path: `/${Pages.Activities}`,
    element: <div>Activities page</div>,
    loader: async () => {
      state.activePage = Pages.Activities;
      return null;
    }
  }
]);

export const urbackupServer = new UrBackupServer("x");
const queryClient = new QueryClient();

const App: React.FunctionComponent = () => {
  const [selectedTheme, setTheme] = useState(initialTheme);

  const snap = useSnapshot(state);

  useEffect(() => {
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', event => {
      setTheme(event.matches ? teamsDarkTheme : teamsLightTheme);
    });
  }, []);

  const styles = useStackStyles();

  return (
      <FluentProvider theme={selectedTheme} style={{ height: "100%" }}>
        <React.StrictMode>
          <QueryClientProvider client={queryClient}>
            <div className={styles.stackVertical}>
              <div className={styles.item}>
                  <HeaderBar />
                </div>
                <div className={styles.itemGrow}>
                  <div className={styles.stackHorizontal}>
                    {snap.loggedIn &&
                      <div className={styles.item} style={{ borderRight: "1px solid", padding: "10pt" }}>
                        <NavSidebar />
                      </div>
                    }
                    <div className={styles.itemGrow} style={{padding: "10pt"}}>
                      <RouterProvider router={router} />
                    </div>
                </div>
              </div>
            </div>
          </QueryClientProvider>
        </React.StrictMode>
      </FluentProvider>
  );
};

export default App;
