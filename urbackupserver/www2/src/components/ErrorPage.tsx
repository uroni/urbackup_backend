import {
  isRouteErrorResponse,
  Navigate,
  useLocation,
  useRouteError,
} from "react-router-dom";

import {
  BackupsAccessDeniedError,
  SessionNotFoundError,
} from "../api/urbackupserver";

export function ErrorPage({ returnToLink }: { returnToLink: React.ReactNode }) {
  const error = useRouteError();
  const { pathname } = useLocation();

  if (error instanceof SessionNotFoundError) {
    return <Navigate to="/login" replace state={{ pathname }} />;
  }

  if (error instanceof BackupsAccessDeniedError) {
    return (
      <article className="flow">
        <h1>Backups Access Denied</h1>
        <ErrorPageContent error={error} />
        <p>Return to {returnToLink}</p>
      </article>
    );
  }

  return (
    <article className="flow">
      <h1>Page not found</h1>
      <ErrorPageContent error={error} />
      <p>Return to {returnToLink}</p>
    </article>
  );
}

function ErrorPageContent({ error }: { error: unknown }) {
  if (isRouteErrorResponse(error)) {
    return (
      <p>
        <i>{error.statusText} </i>
      </p>
    );
  }

  if (error instanceof Error) {
    return (
      <p>
        <i>{error.message}</i>
      </p>
    );
  }
}
