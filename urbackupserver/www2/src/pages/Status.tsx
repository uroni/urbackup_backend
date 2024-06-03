import {
  createTableColumn,
  DataGrid,
  DataGridBody,
  DataGridCell,
  DataGridHeader,
  DataGridHeaderCell,
  DataGridRow,
  makeStyles,
  Select,
  Spinner,
  TableCellLayout,
  TableColumnDefinition,
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
import { StatusMenuGrid, StatusMenuRow } from "../features/status";

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
      <StatusMenuGrid idList={data as StatusClientItem["id"][]} />
    ),
    renderCell: ({ id }) => <StatusMenuRow id={id} />,
  }),
];

const useStyles = makeStyles({
  pageSize: {
    display: "flex",
    alignItems: "center",
    gap: tokens.spacingHorizontalS,
  },
  dataGrid: {
    marginBlockStart: tokens.spacingHorizontalM,
  },
});

const paginationStyles = {
  pageNumber: {
    verticalAlign: "top",
    color: "currentColor",
  },
};

const DEFAULT_PAGE_SIZE = 10;
const PAGE_SIZES = [10, 25, 50, 100];

const Status = () => {
  const statusResult = useQuery("status", urbackupServer.status, {
    suspense: true,
  });

  const [pageSize, setPageSize] = useState<number>(DEFAULT_PAGE_SIZE);
  const [page, setPage] = useState(0);
  const [selectedIds, setSelectedIds] = useState<StatusClientItem["id"][]>([]);

  const classes = useStyles();

  const dataItems = statusResult.data!.status;

  // Hide items with delete_pending === 1
  const filteredItems = dataItems.filter((d) => d.delete_pending !== "1");

  const pageData = chunk(filteredItems, pageSize);

  return (
    <>
      <Suspense fallback={<Spinner />}>
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
              className={classes.dataGrid}
              onSelectionChange={(_e, data) => {
                const clientIds = Array.from(data.selectedItems, Number);

                if (clientIds.length) {
                  setSelectedIds(clientIds);
                }
              }}
            >
              <DataGridHeader>
                <DataGridRow
                  selectionCell={{ "aria-label": "Select all rows" }}
                >
                  {({ renderHeaderCell }) => (
                    <DataGridHeaderCell>
                      {renderHeaderCell(selectedIds)}
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
            <div>
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
            </div>
          </>
        )}
      </Suspense>
    </>
  );
};

export default Status;
