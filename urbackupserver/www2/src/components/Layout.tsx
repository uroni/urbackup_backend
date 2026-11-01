import { Suspense } from "react";
import { Spinner } from "@fluentui/react-components";
import { Outlet, useMatch } from "react-router-dom";

import HeaderBar from "./HeaderBar";
import styles from "./Layout.module.css";
import NavSidebar from "./NavSidebar";
import { SettingsNavSidebar } from "../features/settings/SettingsNavSidebar";

export function BaseLayout({ children }: { children: React.ReactNode }) {
  const match = useMatch("/settings/*");

  return (
    <div className={styles.layout}>
      <HeaderBar />
      <div className={styles.body}>
        {match ? (
          <Suspense fallback={<Spinner />}>
            <SettingsNavSidebar />
          </Suspense>
        ) : (
          <NavSidebar />
        )}
        <Layout.Content>{children}</Layout.Content>
      </div>
    </div>
  );
}

export function Layout() {
  return (
    <BaseLayout>
      <Outlet />
    </BaseLayout>
  );
}

function Content({ children }: { children: React.ReactNode }) {
  const clientMatch = useMatch("/settings/clients/*");

  return (
    <main
      className={`${styles.content} ${clientMatch ? styles["without-padding-content"] : ""}`}
    >
      {children}
    </main>
  );
}

Layout.Content = Content;
