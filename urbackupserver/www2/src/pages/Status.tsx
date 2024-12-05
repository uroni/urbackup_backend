import {
  Button,
  createTableColumn,
  DataGrid,
  DataGridBody,
  DataGridCell,
  DataGridHeader,
  DataGridHeaderCell,
  DataGridRow,
  Field,
  makeStyles,
  MenuButton,
  MenuItem,
  SearchBox,
  Spinner,
  TableCellLayout,
  TableColumnDefinition,
  TableRowId,
  tokens,
} from "@fluentui/react-components";
import { StatusClientItem } from "../api/urbackupserver";
import { Suspense, useState } from "react";
import { useSuspenseQuery } from "@tanstack/react-query";
import { urbackupServer } from "../App";
import {
  BackupResultProvider,
  DownloadClient,
  LastFileBackup,
  LastImageBackup,
  StatusMenuAction,
} from "../features/status";
import { useStatusClientActions } from "../features/status/useStatusClientActions";
import { formatDatetime } from "../utils/format";
import { TableWrapper } from "../components/TableWrapper";
import {
  Pagination,
  PaginationItemsPerPageSelector,
  usePagination,
} from "../components/Pagination";

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
    columnId: "lastFilebackup",
    renderHeaderCell: () => {
      return "Last file backup";
    },
    compare: (a, b) => {
      return compareNum(a.lastbackup, b.lastbackup);
    },
    renderCell: LastFileBackup,
  }),
  createTableColumn<StatusClientItem>({
    columnId: "lastImagebackup",
    renderHeaderCell: () => {
      return "Last image backup";
    },
    compare: (a, b) => {
      return compareNum(a.lastbackup_image, b.lastbackup_image);
    },
    renderCell: LastImageBackup,
  }),
  createTableColumn<StatusClientItem>({
    columnId: "action",
    renderHeaderCell: (data) => (
      <StatusMenuAction idList={data as StatusClientItem["id"][]}>
        {({ removeClients }) => (
          <MenuItem
            onClick={() => removeClients(data as StatusClientItem["id"][])}
          >
            Remove clients
          </MenuItem>
        )}
      </StatusMenuAction>
    ),
    renderCell: ({ id }) => (
      <StatusMenuAction idList={[id]}>
        {({ removeClients }) => (
          <MenuItem onClick={() => removeClients([id])}>
            Remove clients
          </MenuItem>
        )}
      </StatusMenuAction>
    ),
  }),
];

const useStyles = makeStyles({
  topFilters: {
    display: "flex",
    gap: tokens.spacingHorizontalM,
  },
  search: {
    display: "flex",
    alignItems: "center",
    gap: tokens.spacingHorizontalS,
  },
  searchBox: {
    width: "28ch",
  },
  pagination: {
    marginInlineStart: "auto",
  },
  gridActions: {
    display: "flex",
    gap: tokens.spacingHorizontalS,
    flexWrap: "wrap",
  },
});

const REFETCH_INTERVAL = 5000;

const Status = () => {
  const statusResult = useSuspenseQuery({
    queryKey: ["status"],
    queryFn: urbackupServer.status,
    refetchInterval: REFETCH_INTERVAL,
  });
  const { removeClients } = useStatusClientActions();

  const [selectedRows, setSelectedRows] = useState<Set<TableRowId>>(new Set());

  const selectedRowsArray = transformSelectedRows(selectedRows);

  const classes = useStyles();

  const [search, setSearch] = useState("");

  const dataItems = statusResult.data!.status;

  const filteredItems = filterClientData(dataItems, search);

  const { itemsPerPage, setItemsPerPage, pageData, page, setPage } =
    usePagination(filteredItems);

  return (
    <>
      <Suspense fallback={<Spinner />}>
        <TableWrapper>
          <h3>Status page</h3>
          <div className={classes.topFilters}>
            <Field label="Search" className={classes.search}>
              <SearchBox
                autoComplete="off"
                className={classes.searchBox}
                onChange={(_, data) => {
                  const search = data.value.toLowerCase();

                  setSearch(search);
                }}
              />
            </Field>
            <PaginationItemsPerPageSelector
              itemsPerPage={itemsPerPage}
              setItemsPerPage={setItemsPerPage}
            />
          </div>
          {pageData.length === 0 ? null : (
            <>
              <DataGrid
                sortable
                selectionMode="multiselect"
                items={pageData[page]}
                getRowId={(item) => item.id}
                columns={columns}
                selectedItems={selectedRows}
                onSelectionChange={(_e, data) => {
                  setSelectedRows(data.selectedItems);
                }}
              >
                <DataGridHeader>
                  <DataGridRow
                    selectionCell={{ "aria-label": "Select all rows" }}
                  >
                    {({ renderHeaderCell }) => (
                      <DataGridHeaderCell>
                        {renderHeaderCell(selectedRowsArray)}
                      </DataGridHeaderCell>
                    )}
                  </DataGridRow>
                </DataGridHeader>
                <DataGridBody<StatusClientItem>>
                  {({ item }) => (
                    <DataGridRow<StatusClientItem>
                      key={item.id}
                      selectionCell={{ "aria-label": "Select row" }}
                    >
                      {({ renderCell }) => (
                        <DataGridCell>{renderCell(item)}</DataGridCell>
                      )}
                    </DataGridRow>
                  )}
                </DataGridBody>
              </DataGrid>
              <Pagination
                pageCount={pageData.length}
                page={page}
                itemsPerPage={itemsPerPage}
                totalItemCount={filteredItems.length}
                setPage={setPage}
              />
              <div className={classes.gridActions}>
                <Button onClick={() => setItemsPerPage(filteredItems.length)}>
                  Show All Clients
                </Button>
                <div>
                  <Button
                    onClick={() => {
                      const allRows = new Set(
                        filteredItems.map(({ id }) => id),
                      );

                      setSelectedRows(allRows);
                    }}
                  >
                    Select All
                  </Button>
                  <Button onClick={() => setSelectedRows(new Set())}>
                    Select None
                  </Button>
                  <Button
                    onClick={() => {
                      if (selectedRowsArray.length) {
                        removeClients(selectedRowsArray);
                      }
                    }}
                  >
                    Remove Selected
                  </Button>
                  <StatusMenuAction
                    idList={selectedRowsArray}
                    trigger={<MenuButton>With Selected</MenuButton>}
                  />
                </div>
                <div>
                  <DownloadClient clients={filteredItems} os="windows">
                    Download client for Windows
                  </DownloadClient>
                  <DownloadClient clients={filteredItems} os="linux">
                    Download client for Linux
                  </DownloadClient>
                </div>
              </div>
            </>
          )}
        </TableWrapper>
      </Suspense>
    </>
  );
};

const StatusPage = () => (
  <BackupResultProvider>
    <Status />
  </BackupResultProvider>
);

export default StatusPage;

function transformSelectedRows(selectedRows: Set<TableRowId>) {
  const clientIds = Array.from(selectedRows, Number);
  return clientIds;
}

function filterClientData(dataItems: StatusClientItem[], search: string) {
  return dataItems.filter((d) => {
    // Hide items scheduled for delete
    if (d.delete_pending === "1") {
      return false;
    }

    // If there's a search term, filter by search term within object values
    if (search.length) {
      const { id, name, lastbackup, lastbackup_image } = d;

      // Search in fields as displayed in the table
      const searchableFields = {
        id,
        name,
        lastbackup: formatDatetime(lastbackup),
        lastbackup_image: formatDatetime(lastbackup_image),
      };

      // Find matching search term in data values
      const match = Object.values(searchableFields).some((v) =>
        String(v).toLowerCase().includes(search),
      );

      return match;
    }

    return true;
  });
}
