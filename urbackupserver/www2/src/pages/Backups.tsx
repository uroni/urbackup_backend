import { Spinner } from "@fluentui/react-components";
import { Suspense } from "react";
import { Outlet } from "react-router-dom";

export const BackupsPage = () => {
  return (
    <Suspense fallback={<Spinner />}>
      <div>
        <Outlet />
      </div>
    </Suspense>
  );
};
