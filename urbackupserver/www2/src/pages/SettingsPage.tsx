import { Suspense } from "react";
import { Spinner } from "@fluentui/react-components";
import { Outlet } from "react-router-dom";

export const SettingsPage = () => {
  return (
    <Suspense fallback={<Spinner />}>
      <div>
        <Outlet />
      </div>
    </Suspense>
  );
};
