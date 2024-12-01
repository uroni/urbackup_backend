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
  makeStyles,
} from "@fluentui/react-components";
import { Link } from "react-router-dom";
import { useSuspenseQuery } from "@tanstack/react-query";

import type { BackupsClient } from "../../api/urbackupserver";
import { getCellFocusMode } from "../../utils/table";
import { formatDatetime } from "../../utils/format";
import { urbackupServer } from "../../App";
import { TableWrapper } from "../../components/TableWrapper";

const useStyles = makeStyles({
  cell: {
    alignItems: "unset",
  },
  link: {
    color: "inherit",
    textDecoration: "none",
    display: "flex",
    alignItems: "center",
    width: "100%",
  },
});

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

const REFETCH_INTERVAL = 5000;

export function BackupsTable() {
  const backupClientsResult = useSuspenseQuery({
    queryKey: ["backups"],
    queryFn: () => urbackupServer.getBackupsClients(),
    refetchInterval: REFETCH_INTERVAL,
  });

  const data = backupClientsResult.data!.clients;

  const classes = useStyles();

  if (data.length === 0) {
    return <span>No activities</span>;
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
                  className={classes.cell}
                >
                  <Link to={String(item.id)} className={classes.link}>
                    {renderCell(item)}
                  </Link>
                </DataGridCell>
              )}
            </DataGridRow>
          )}
        </DataGridBody>
      </DataGrid>
    </TableWrapper>
  );
}
