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
} from "@fluentui/react-components";
import { Link } from "react-router-dom";
import { useSuspenseQuery } from "@tanstack/react-query";

import type { BackupsClient } from "../../api/urbackupserver";
import { getCellFocusMode } from "../../utils/table";
import { formatDatetime } from "../../utils/format";
import { urbackupServer } from "../../App";
import { TableWrapper } from "../../components/TableWrapper";

export const columns: TableColumnDefinition<BackupsClient>[] = [
  createTableColumn<BackupsClient>({
    columnId: "name",
    renderHeaderCell: () => {
      return "Computer name";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.name}</TableCellLayout>;
    },
  }),
  createTableColumn<BackupsClient>({
    columnId: "lastFilebackup",
    renderHeaderCell: () => {
      return "Last file backup";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          {item.lastbackup ? formatDatetime(item.lastbackup) : "-"}
        </TableCellLayout>
      );
    },
  }),
];

export function BackupsTable() {
  const backupClientsResult = useSuspenseQuery({
    queryKey: ["backups"],
    queryFn: () => urbackupServer.getBackupsClients(),
  });

  const data = backupClientsResult.data!.clients;

  if (data.length === 0) {
    return <span>No clients</span>;
  }

  return (
    <TableWrapper>
      <h3>Backups</h3>
      <DataGrid items={data} getRowId={(item) => item.id} columns={columns}>
        <DataGridHeader>
          <DataGridRow>
            {({ renderHeaderCell }) => (
              <DataGridHeaderCell>{renderHeaderCell()}</DataGridHeaderCell>
            )}
          </DataGridRow>
        </DataGridHeader>
        <DataGridBody<BackupsClient>>
          {({ item }) => (
            <DataGridRow<BackupsClient> key={item.id}>
              {({ renderCell, columnId }) => (
                <DataGridCell
                  focusMode={getCellFocusMode(columnId, {
                    none: ["name", "lastFilebackup"],
                  })}
                >
                  <Link to={String(item.id)}>{renderCell(item)}</Link>
                </DataGridCell>
              )}
            </DataGridRow>
          )}
        </DataGridBody>
      </DataGrid>
    </TableWrapper>
  );
}
