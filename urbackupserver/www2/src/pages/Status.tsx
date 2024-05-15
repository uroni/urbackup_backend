import {
  createTableColumn,
  DataGrid,
  DataGridBody,
  DataGridCell,
  DataGridHeader,
  DataGridHeaderCell,
  DataGridRow,
  Spinner,
  TableCellLayout,
  TableColumnDefinition,
} from "@fluentui/react-components";
import { Suspense} from "react";
import { useQuery } from "react-query";
import { StatusClientItem } from "../api/urbackupserver";
import { urbackupServer } from "../App";

const compareNum = (a: number, b: number) => {
  return a == b ? 0 : a < b ? 1 : -1;
};

const columns: TableColumnDefinition<StatusClientItem>[] = [
  createTableColumn<StatusClientItem>({
    columnId: "id",
    renderHeaderCell: () => {
      return "Id";
    },
    compare: (a, b) => {
      return compareNum(a.id, b.id);
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.id}</TableCellLayout>;
    },
  }),
  createTableColumn<StatusClientItem>({
    columnId: "name",
    renderHeaderCell: () => {
      return "Client name";
    },
    compare: (a, b) => {
      return a.name.localeCompare(b.name);
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.name}</TableCellLayout>;
    },
  }),
  createTableColumn<StatusClientItem>({
    columnId: "lastImagebackup",
    renderHeaderCell: () => {
      return "Last image backup";
    },
    compare: (a, b) => {
      return compareNum(a.lastbackup_image, b.lastbackup_image);
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          {new Date(item.lastbackup_image * 1000).toLocaleString()}
        </TableCellLayout>
      );
    },
  }),
  createTableColumn<StatusClientItem>({
    columnId: "lastFilebackup",
    renderHeaderCell: () => {
      return "Last file backup";
    },
    compare: (a, b) => {
      return compareNum(a.lastbackup, b.lastbackup);
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          {new Date(item.lastbackup * 1000).toLocaleString()}
        </TableCellLayout>
      );
    },
  }),
];

const Status = () => {
  const statusResult = useQuery("status", urbackupServer.status, {
    suspense: true,
  });

  return (
    <>
      <Suspense fallback={<Spinner />}>
        <h3>Status page</h3>
        <DataGrid
          sortable
          selectionMode="multiselect"
          items={statusResult.data!.status}
          getRowId={(item) => item.id}
          columns={columns}
        >
          <DataGridHeader>
            <DataGridRow selectionCell={{ "aria-label": "Select all rows" }}>
              {({ renderHeaderCell }) => (
                <DataGridHeaderCell>{renderHeaderCell()}</DataGridHeaderCell>
              )}
            </DataGridRow>
          </DataGridHeader>
          <DataGridBody<StatusClientItem>>
            {({ item, rowId }) => (
              <DataGridRow<StatusClientItem>
                key={rowId}
                selectionCell={{ "aria-label": "Select row" }}
              >
                {({ renderCell }) => (
                  <DataGridCell>{renderCell(item)}</DataGridCell>
                )}
              </DataGridRow>
            )}
          </DataGridBody>
        </DataGrid>
      </Suspense>
    </>
  );
};

export default Status;
