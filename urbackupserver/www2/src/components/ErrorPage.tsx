import { useRouteError } from "react-router-dom";

import { BackupsAccessDeniedError } from "../api/urbackupserver";

export function ErrorPage({ returnToLink }: { returnToLink: React.ReactNode }) {
  const error = useRouteError();

  if (error instanceof BackupsAccessDeniedError) {
    return (
      <article className="flow">
        <h1>Backups Access Denied</h1>
        <p>
          <i>{error.statusText || error.message}</i>
        </p>
        <p>Return to {returnToLink}</p>
      </article>
    );
  }

  return (
    <article className="flow">
      <h1>Page not found</h1>
      <p>
        <i>{error.statusText || error.message}</i>
      </p>
      <p>Return to {returnToLink}</p>
    </article>
  );
}
