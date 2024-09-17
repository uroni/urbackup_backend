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
  Select,
  Spinner,
  TableCellLayout,
  TableColumnDefinition,
  TableRowId,
  tokens,
} from "@fluentui/react-components";
import { StatusClientItem } from "../api/urbackupserver";
import { Suspense, useState } from "react";
import { useSuspenseQuery } from "@tanstack/react-query";
import { Pagination } from "@fluentui/react-experiments";
import { urbackupServer } from "../App";
import { chunk } from "../utils/chunk";
import { registerIcons } from "@fluentui/react-experiments/lib/Styling";
import {
  ArrowNext20Filled,
  ArrowPrevious20Filled,
  ChevronLeft20Filled,
  ChevronRight20Filled,
} from "@fluentui/react-icons";
import {
  BackupResultProvider,
  DownloadClient,
  LastFileBackup,
  LastImageBackup,
  StatusMenuAction,
} from "../features/status";
import { useStatusClientActions } from "../features/status/useStatusClientActions";
import { formatDatetime } from "../features/status/formatDatetime";

// Register icons used in Pagination @fluentui/react-experiments. See https://github.com/microsoft/fluentui/wiki/Using-icons#registering-custom-icons.
registerIcons({
  icons: {
    CaretSolidLeft: <ChevronLeft20Filled />,
    CaretSolidRight: <ChevronRight20Filled />,
    Next: <ArrowNext20Filled />,
    Previous: <ArrowPrevious20Filled />,
  },
});

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
  root: {
    display: "grid",
    gap: tokens.spacingHorizontalL,
  },
  heading: {
    marginBlockStart: 0,
  },
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
  pageSize: {
    display: "flex",
    alignItems: "center",
    gap: tokens.spacingHorizontalS,
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

const paginationStyles = {
  root: {
    alignItems: "end",
    marginBlockStart: tokens.spacingHorizontalM,
  },
  pageNumber: {
    verticalAlign: "top",
    color: "currentColor",
  },
};

const PAGE_SIZES = [10, 25, 50, 100];
const DEFAULT_PAGE_SIZE = PAGE_SIZES[0];
const REFETCH_INTERVAL = 5000;

const Status = () => {
  const statusResult = useSuspenseQuery({
    queryKey: ["status"],
    queryFn: urbackupServer.status,
    refetchInterval: REFETCH_INTERVAL,
  });
  const { removeClients } = useStatusClientActions();

  const [pageSize, setPageSize] = useState<number>(DEFAULT_PAGE_SIZE);
  const [page, setPage] = useState(0);
  const [selectedRows, setSelectedRows] = useState<Set<TableRowId>>(new Set());

  const selectedRowsArray = transformSelectedRows(selectedRows);

  const classes = useStyles();

  const [search, setSearch] = useState("");

  const dataItems = statusResult.data!.status;

  const filteredItems = filterClientData(dataItems, search);

  const pageData = chunk(filteredItems, pageSize);

  return (
    <>
      <Suspense fallback={<Spinner />}>
        <div className={classes.root}>
          <div>
            <h3 className={classes.heading}>Status page</h3>
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
              <label className={classes.pageSize}>
                Show
                <Select
                  id="page-size"
                  defaultValue={pageSize}
                  onChange={(_, data) => setPageSize(+data.value)}
                >
                  {PAGE_SIZES.map((size, id) => (
                    <option key={id}>{size}</option>
                  ))}
                </Select>
                entries
              </label>
            </div>
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
                selectedPageIndex={page}
                pageCount={pageData.length}
                itemsPerPage={pageSize}
                totalItemCount={filteredItems.length}
                format={"buttons"}
                previousPageAriaLabel={"previous page"}
                nextPageAriaLabel={"next page"}
                firstPageAriaLabel={"first page"}
                lastPageAriaLabel={"last page"}
                pageAriaLabel={"page"}
                selectedAriaLabel={"selected"}
                onPageChange={(index) => setPage(index)}
                styles={paginationStyles}
              />
              <div className={classes.gridActions}>
                <Button onClick={() => setPageSize(filteredItems.length)}>
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
        </div>
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
