import { Suspense } from "react";
import { Spinner } from "@fluentui/react-components";

import { TableWrapper } from "../components/TableWrapper";
import { ClientLogs } from "../features/logs/ClientLogs";

export const LogsPage = () => {
  return (
    <Suspense fallback={<Spinner />}>
      <TableWrapper>
        <h3>Logs</h3>
        <ClientLogs />
      </TableWrapper>
    </Suspense>
  );
};
