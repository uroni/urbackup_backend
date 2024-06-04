import {
  Button,
  createTableColumn,
  DataGrid,
  DataGridBody,
  DataGridCell,
  DataGridHeader,
  DataGridHeaderCell,
  DataGridRow,
  makeStyles,
  MenuButton,
  MenuItem,
  Select,
  Spinner,
  TableCellLayout,
  TableColumnDefinition,
  TableRowId,
  tokens,
} from "@fluentui/react-components";
import { StatusClientItem } from "../api/urbackupserver";
import { Suspense, useState } from "react";
import { useQuery } from "react-query";
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
import { StatusMenuAction } from "../features/status";
import { useStatusClientActions } from "../features/status/useStatusClientActions";

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

const Status = () => {
  const statusResult = useQuery("status", urbackupServer.status, {
    suspense: true,
  });
  const { removeClients } = useStatusClientActions();

  const [pageSize, setPageSize] = useState<number>(DEFAULT_PAGE_SIZE);
  const [page, setPage] = useState(0);
  const [selectedRows, setSelectedRows] = useState<Set<TableRowId>>(new Set());

  const selectedRowsArray = transformSelectedRows(selectedRows);

  const classes = useStyles();

  const dataItems = statusResult.data!.status;

  // Hide items with delete_pending === 1
  const filteredItems = dataItems.filter((d) => d.delete_pending !== "1");

  const pageData = chunk(filteredItems, pageSize);

  return (
    <>
      <Suspense fallback={<Spinner />}>
        <div className={classes.root}>
          <h3>Status page</h3>
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
              </div>
            </>
          )}
        </div>
      </Suspense>
    </>
  );
};

export default Status;

function transformSelectedRows(selectedRows: Set<TableRowId>) {
  if (selectedRows.size) {
    const clientIds = Array.from(selectedRows, Number);

    return clientIds;
  }

  return [];
}
