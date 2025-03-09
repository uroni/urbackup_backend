import {
  DataGrid,
  DataGridHeader,
  DataGridRow,
  DataGridHeaderCell,
  DataGridBody,
  DataGridCell,
  TableCellLayout,
  TableColumnDefinition,
  createTableColumn,
  tokens,
  TableColumnId,
} from "@fluentui/react-components";
import { Link } from "react-router-dom";
import { useSuspenseQuery } from "@tanstack/react-query";
import { DismissCircle16Filled, Warning16Filled } from "@fluentui/react-icons";

import {
  LOG_LEVELS,
  type ClientIdType,
  type LogInfo,
} from "../../api/urbackupserver";
import { formatDatetime } from "../../utils/format";
import { getCellFocusMode } from "../../utils/table";
import { urbackupServer } from "../../App";
import { getActionFromBackup } from "../../utils/getActionFromBackup";

const columns: TableColumnDefinition<LogInfo>[] = [
  createTableColumn<LogInfo>({
    columnId: "name",
    renderHeaderCell: () => {
      return "Computer name";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.name}</TableCellLayout>;
    },
  }),
  createTableColumn<LogInfo>({
    columnId: "time",
    renderHeaderCell: () => {
      return "Backup time";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatDatetime(item.time)}</TableCellLayout>;
    },
  }),
  createTableColumn<LogInfo>({
    columnId: "action",
    renderHeaderCell: () => {
      return "Action";
    },
    renderCell: (item) => {
      return <TableCellLayout>{getActionFromBackup(item)}</TableCellLayout>;
    },
  }),
  createTableColumn<LogInfo>({
    columnId: "errors",
    renderHeaderCell: () => {
      return "Errors";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          <div className="cluster" data-spacing="s">
            {item.errors > 0 && (
              <DismissCircle16Filled
                style={{
                  color: tokens.colorStatusDangerForeground1,
                }}
              />
            )}
            {item.errors}
          </div>
        </TableCellLayout>
      );
    },
  }),
  createTableColumn<LogInfo>({
    columnId: "warnings",
    renderHeaderCell: () => {
      return "Warnings";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          <div className="cluster" data-spacing="s">
            {item.warnings > 0 && (
              <Warning16Filled
                style={{
                  color: tokens.colorStatusWarningForeground1,
                }}
              />
            )}
            {item.warnings}
          </div>
        </TableCellLayout>
      );
    },
  }),
];

export function LogsTable({
  selectedClientId,
  logLevel,
}: {
  selectedClientId?: ClientIdType;
  logLevel: (typeof LOG_LEVELS)[keyof typeof LOG_LEVELS];
}) {
  const logsResult = useSuspenseQuery({
    queryKey: ["logs", logLevel, selectedClientId],
    queryFn: () =>
      urbackupServer.getLogs(
        selectedClientId ? [selectedClientId] : [],
        logLevel,
      ),
  });

  const { logs } = logsResult.data;

  if (logs.length === 0) {
    return <div>No entries for this filter</div>;
  }

  return (
    <DataGrid items={logs} getRowId={(item) => item.id} columns={columns}>
      <DataGridHeader>
        <DataGridRow>
          {({ renderHeaderCell, columnId }) => (
            <DataGridHeaderCell style={getNarrowColumnStyles(columnId)}>
              {renderHeaderCell()}
            </DataGridHeaderCell>
          )}
        </DataGridRow>
      </DataGridHeader>
      <DataGridBody<LogInfo>>
        {({ item }) => (
          <DataGridRow<LogInfo> key={item.id}>
            {({ renderCell, columnId }) => (
              <DataGridCell
                focusMode={getCellFocusMode(columnId, {
                  none: ["name", "backuptime", "action", "errors", "warnings"],
                })}
                style={getNarrowColumnStyles(columnId)}
              >
                <Link to={String(item.id)}>{renderCell(item)}</Link>
              </DataGridCell>
            )}
          </DataGridRow>
        )}
      </DataGridBody>
    </DataGrid>
  );
}

/**
 * Style some columns to take up less space.
 */
function getNarrowColumnStyles(columnId: TableColumnId) {
  const stringId = columnId.toString();

  return {
    flexGrow: ["errors", "warnings"].includes(stringId) ? "0" : "1",
    flexBasis: ["errors", "warnings"].includes(stringId) ? "14ch" : "0",
  };
}
