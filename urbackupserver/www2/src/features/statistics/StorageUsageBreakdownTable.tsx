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
  DataGridProps,
} from "@fluentui/react-components";

import type { UsageClientStat } from "../../api/urbackupserver";
import { format_size } from "../../utils/format";
import { useSuspenseQuery } from "@tanstack/react-query";
import { urbackupServer } from "../../App";
import {
  filterBySearch,
  SearchBox,
  useFilteredBySearch,
} from "../../components/SearchBox";
import {
  Pagination,
  PaginationItemsPerPageSelector,
  usePagination,
} from "../../components/Pagination";
import { TableWrapper } from "../../components/TableWrapper";
import { useState } from "react";

const compareNum = (a: number, b: number) => {
  return a == b ? 0 : a < b ? -1 : 1;
};

export const columns: TableColumnDefinition<UsageClientStat>[] = [
  createTableColumn<UsageClientStat>({
    columnId: "name",
    compare: (a, b) => {
      return a.name.localeCompare(b.name);
    },
    renderHeaderCell: () => {
      return "Computer name";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.name}</TableCellLayout>;
    },
  }),
  createTableColumn<UsageClientStat>({
    columnId: "images",
    compare: (a, b) => {
      return compareNum(a.images, b.images);
    },
    renderHeaderCell: () => {
      return "Images";
    },
    renderCell: (item) => {
      return <TableCellLayout>{format_size(item.images ?? 0)}</TableCellLayout>;
    },
  }),
  createTableColumn<UsageClientStat>({
    columnId: "files",
    compare: (a, b) => {
      return compareNum(a.files, b.files);
    },
    renderHeaderCell: () => {
      return "Files";
    },
    renderCell: (item) => {
      return <TableCellLayout>{format_size(item.files ?? 0)}</TableCellLayout>;
    },
  }),
  createTableColumn<UsageClientStat>({
    columnId: "used",
    compare: (a, b) => {
      return compareNum(a.used, b.used);
    },
    renderHeaderCell: () => {
      return "All";
    },
    renderCell: (item) => {
      return <TableCellLayout>{format_size(item.used ?? 0)}</TableCellLayout>;
    },
  }),
];

export function StorageUsageBreakdownTable() {
  const storageUsageStatsResult = useSuspenseQuery({
    queryKey: ["storage-usage"],
    queryFn: () => urbackupServer.getUsageStats(),
  });

  const data = storageUsageStatsResult.data!.usage;

  if (data.length === 0) {
    return <span>No storage in use</span>;
  }

  const [sortState, setSortState] =
    useState<Parameters<NonNullable<DataGridProps["onSortChange"]>>[1]>();

  const onSortChange: DataGridProps["onSortChange"] = (e) => {
    e.preventDefault();
  };

  const [sortedData, setSortedData] = useState(data);

  const { setSearch, filteredItems } = useFilteredBySearch<UsageClientStat>(
    sortedData,
    filterData,
  );

  const { itemsPerPage, setItemsPerPage, pageData, page, setPage } =
    usePagination(filteredItems);

  return (
    <TableWrapper>
      <div>
        <h4>Storage Usage Breakdown</h4>
      </div>
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
            items={pageData[page]}
            getRowId={(item) => item.id}
            columns={columns}
          >
            <DataGridHeader>
              <DataGridRow>
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
                    {renderHeaderCell()}
                  </DataGridHeaderCell>
                )}
              </DataGridRow>
            </DataGridHeader>
            <DataGridBody<UsageClientStat>>
              {({ item }) => (
                <DataGridRow<UsageClientStat> key={item.name}>
                  {({ renderCell }) => (
                    <DataGridCell align="right">
                      {renderCell(item)}
                    </DataGridCell>
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
        </>
      )}
    </TableWrapper>
  );
}

function filterData(item: UsageClientStat, search: string) {
  const { name, files, images, used } = item;

  const searchableFields = {
    name,
    files: String(files),
    images: String(images),
    used: String(used),
  };

  return filterBySearch(search, searchableFields);
}
