import { Suspense, useState } from "react";
import { Spinner, Select } from "@fluentui/react-components";
import { useSuspenseQuery } from "@tanstack/react-query";

import { LOG_LEVELS, type ClientIdType } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";
import { SelectClientCombobox } from "../../components/SelectClientCombobox";
import { LogsTable } from "./LogsTable";
import { TableWrapper } from "../../components/TableWrapper";
import { LiveLog } from "./LiveLog";
import { LogReports } from "./LogReports";
import styles from "./ClientLogs.module.css";

const FORMATTED_LOG_LEVELS = {
  INFO: "All",
  WARNING: "Warnings",
  ERROR: "Errors",
} as const;

export function ClientLogs() {
  const [selectedClientId, setSelectedClientId] = useState<
    ClientIdType | undefined
  >();

  const [logLevel, setLogLevel] = useState<
    (typeof LOG_LEVELS)[keyof typeof LOG_LEVELS]
  >(LOG_LEVELS.ERROR);

  // Used for fetching clients list for logs
  const logsResult = useSuspenseQuery({
    queryKey: ["logs"],
    queryFn: () => urbackupServer.getLogs([], LOG_LEVELS.INFO),
  });

  const { clients } = logsResult.data;

  return (
    <div className={styles.root}>
      <TableWrapper>
        <div className="repel heading-breadcrumbs">
          <h3>Logs</h3>
          <LiveLog clients={clients}>Open Live Log</LiveLog>
        </div>
        <div className="cluster">
          <SelectClientCombobox
            clients={clients}
            onSelect={(id) => setSelectedClientId(Number(id))}
          />
          <div className="cluster gutter-s">
            Filter
            <Select
              id="log-level"
              defaultValue={logLevel}
              onChange={(_, data) =>
                setLogLevel(+data.value as typeof logLevel)
              }
            >
              {Object.entries(LOG_LEVELS).map(([k, v]) => (
                <option key={k} value={v}>
                  {FORMATTED_LOG_LEVELS[k as keyof typeof LOG_LEVELS]}
                </option>
              ))}
            </Select>
          </div>
        </div>
        <Suspense fallback={<Spinner />}>
          <LogsTable selectedClientId={selectedClientId} logLevel={logLevel} />
        </Suspense>
      </TableWrapper>
      <div className={`${styles.report} flow`}>
        <h4>Reports</h4>
        <p>Automatically send reports to emails.</p>
        <Suspense fallback={<Spinner />}>
          <LogReports />
        </Suspense>
      </div>
    </div>
  );
}
