import { useState } from "react";
import {
  Select,
  Text,
  makeStyles,
  tokens,
  mergeClasses,
} from "@fluentui/react-components";
import { useSuspenseQuery } from "@tanstack/react-query";
import { useParams } from "react-router-dom";

import { LOG_LEVELS } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";
import { TableWrapper } from "../../components/TableWrapper";
import {
  BASE_HREF,
  BreadcrumbItem,
  Breadcrumbs,
} from "../../components/Breadcrumbs";
import { FORMATTED_LOG_TABLE_LEVELS, LogTable } from "./LogTable";

const logsUrl = `${BASE_HREF}/logs`;

const useStyles = makeStyles({
  heading: {
    display: "flex",
    gap: tokens.spacingHorizontalM,
    alignItems: "center",
    // Negate the margin top and left values
    // to keep breadcrumbs aligned to initial top-left position
    marginInlineStart: "-7px",
    marginBlockStart: "-7px",
  },
  filters: {
    marginTop: `calc(1em + 3px)`,
  },
});

export function ClientLog() {
  const { logId } = useParams();

  const [logLevel, setLogLevel] = useState<
    (typeof LOG_LEVELS)[keyof typeof LOG_LEVELS]
  >(LOG_LEVELS.ERROR);

  // Used for fetching clients list for logs
  const logResult = useSuspenseQuery({
    queryKey: ["log", logId],
    queryFn: async () => {
      try {
        return await urbackupServer.getLog(Number(logId));
      } catch (e) {
        throw new Error(`No such log with ID: ${logId}`);
      }
    },
  });

  const classes = useStyles();

  const { log } = logResult.data;

  const { clientname, data } = log;

  const breadcrumbItems: BreadcrumbItem[] = [
    {
      key: "logs",
      text: "Logs",
      itemProps: {
        href: logsUrl,
      },
    },
    {
      key: `log-${logId}`,
      text: clientname,
      itemProps: {
        href: "",
      },
    },
  ];

  const filteredData = data.filter((d) => d.level >= logLevel);

  return (
    <TableWrapper>
      <div className={classes.heading}>
        <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
        <Text
          font="numeric"
          style={{
            color: "var(--colorNeutralForeground3)",
          }}
        >
          ID #{logId}
        </Text>
      </div>
      <div
        className={mergeClasses(classes.filters, "cluster")}
        data-spacing="s"
      >
        Filter
        <Select
          id="log-level"
          defaultValue={logLevel}
          onChange={(_, data) => setLogLevel(+data.value as typeof logLevel)}
        >
          {Object.entries(LOG_LEVELS).map(([k, v]) => (
            <option key={k} value={v}>
              {FORMATTED_LOG_TABLE_LEVELS[k as keyof typeof LOG_LEVELS]}
            </option>
          ))}
        </Select>
      </div>
      <LogTable data={filteredData} />
    </TableWrapper>
  );
}
