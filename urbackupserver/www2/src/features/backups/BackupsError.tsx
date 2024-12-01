import { useRouteError } from "react-router-dom";
import { BackupsAccessDeniedError } from "../../api/urbackupserver";
import { Link } from "@fluentui/react-components";

export default function BackupErrorPage() {
  const error = useRouteError();

  if (error instanceof BackupsAccessDeniedError) {
    return (
      <article className="flow">
        <h1>Backups Access Denied</h1>
        <p>
          <i>{error.statusText || error.message}</i>
        </p>
        <p>
          Return to <Link href="/#/backups">Backups</Link>.
        </p>
      </article>
    );
  }

  return (
    <article className="flow">
      <h1>Page not found</h1>
      <p>
        <i>{error.statusText || error.message}</i>
      </p>
      <p>
        Return to <Link href="/#/backups">Backups</Link>.
      </p>
    </article>
  );
}
