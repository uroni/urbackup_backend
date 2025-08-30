import * as React from "react";
import { useLocation, Navigate } from "react-router-dom";

import { useUser } from "../pages/Login";

export function AuthenticatedRoute({
  children,
}: {
  children: React.ReactNode;
}) {
  const { pathname } = useLocation();
  const { session } = useUser();

  if (!session) {
    return <Navigate to="/login" replace state={{ pathname }} />;
  }

  return children;
}
