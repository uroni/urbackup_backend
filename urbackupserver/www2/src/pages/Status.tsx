import {
  Button,
  createTableColumn,
  DataGrid,
  DataGridBody,
  DataGridCell,
  DataGridHeader,
  DataGridHeaderCell,
  DataGridProps,
  DataGridRow,
  makeStyles,
  MenuButton,
  MenuItem,
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
import {
  filterBySearch,
  SearchBox,
  useFilteredBySearch,
} from "../components/SearchBox";

const compareNum = (a: number, b: number) => {
  return a == b ? 0 : a < b ? -1 : 1;
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

  const data = statusResult.data!.status;

  const [sortState, setSortState] =
    useState<Parameters<NonNullable<DataGridProps["onSortChange"]>>[1]>();

  const onSortChange: DataGridProps["onSortChange"] = (e) => {
    e.preventDefault();
  };

  const [sortedData, setSortedData] = useState(data);

  const { setSearch, filteredItems } = useFilteredBySearch<StatusClientItem>(
    sortedData,
    filterClientData,
  );

  const { itemsPerPage, setItemsPerPage, pageData, page, setPage } =
    usePagination(filteredItems);

  return (
    <>
      <Suspense fallback={<Spinner />}>
        <TableWrapper>
          <h3>Status page</h3>
          <div className="cluster">
            <SearchBox onSearch={setSearch} />
            <PaginationItemsPerPageSelector
              itemsPerPage={itemsPerPage}
              setItemsPerPage={setItemsPerPage}
            />
          </div>
          {pageData.length === 0 ? null : (
            <>
              <DataGrid
                key={`${sortState?.sortColumn}-${sortState?.sortDirection}`}
                sortable
                onSortChange={onSortChange}
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
                    {({ renderHeaderCell, columnId, compare }) => (
                      <DataGridHeaderCell
                        sortDirection={
                          sortState?.sortColumn === columnId
                            ? sortState.sortDirection
                            : undefined
                        }
                        button={{
                          onClick: () => {
                            const newSortState: typeof sortState = {
                              sortColumn: columnId,
                              sortDirection:
                                sortState?.sortDirection === "descending" ||
                                sortState?.sortColumn !== columnId
                                  ? "ascending"
                                  : "descending",
                            };

                            if (newSortState.sortDirection === "ascending") {
                              const newSortedData = sortedData.sort(compare);
                              setSortedData(newSortedData);
                            }

                            if (newSortState.sortDirection === "descending") {
                              const newSortedData = sortedData.sort((a, b) =>
                                // Reverse compare function params for descending order
                                compare(b, a),
                              );
                              setSortedData(newSortedData);
                            }

                            setSortState(newSortState);
                          },
                        }}
                      >
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

function filterClientData(item: StatusClientItem, search: string) {
  // Hide items scheduled for delete
  if (item.delete_pending === "1") {
    return false;
  }

  const { id, name, lastbackup, lastbackup_image } = item;

  // Search in fields as displayed in the table
  const searchableFields = {
    id: String(id),
    name,
    lastbackup: lastbackup === 0 ? "Never" : formatDatetime(lastbackup),
    lastbackup_image:
      lastbackup === 0 ? "Never" : formatDatetime(lastbackup_image),
  };

  return filterBySearch(search, searchableFields);
}
