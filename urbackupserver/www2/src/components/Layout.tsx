import { Suspense } from "react";
import { Spinner } from "@fluentui/react-components";
import { Outlet, useMatch } from "react-router-dom";

import HeaderBar from "./HeaderBar";
import styles from "./Layout.module.css";
import NavSidebar from "./NavSidebar";
import { SettingsNavSidebar } from "../features/settings/SettingsNavSidebar";

export function Layout() {
  const match = useMatch("/settings/*");

  return (
    <div className={styles.layout}>
      <Layout.Sidebar>
        {match ? (
          <Suspense fallback={<Spinner />}>
            <SettingsNavSidebar />
          </Suspense>
        ) : (
          <NavSidebar />
        )}
      </Layout.Sidebar>
      <Layout.Content>
        <Outlet />
      </Layout.Content>
    </div>
  );
}

function Sidebar({ children }: { children: React.ReactNode }) {
  return (
    <header className={`${styles.sidebar} flow`}>
      <HeaderBar />
      {children}
    </header>
  );
}

function Content({ children }: { children: React.ReactNode }) {
  return <main className={styles.content}>{children}</main>;
}

Layout.Sidebar = Sidebar;

Layout.Content = Content;
