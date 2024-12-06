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
import {
  Pagination,
  PaginationItemsPerPageSelector,
  usePagination,
} from "../../components/Pagination";
import {
  filterBySearch,
  SearchBox,
  useFilteredBySearch,
} from "../../components/SearchBox";

function createFormatter<T extends BackupsClient>() {
  return {
    lastbackup: (d: T) => (d.lastbackup ? formatDatetime(d.lastbackup) : "-"),
  } as Record<keyof T, (d: T) => string>;
}

const formatter = createFormatter();

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
      return <TableCellLayout>{formatter.lastbackup(item)}</TableCellLayout>;
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

  const { setSearch, filteredItems } = useFilteredBySearch<BackupsClient>(
    data,
    filterData,
  );

  const { itemsPerPage, setItemsPerPage, pageData, page, setPage } =
    usePagination(filteredItems);

  return (
    <TableWrapper>
      <h3>Backups</h3>
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
            items={pageData[page]}
            getRowId={(item) => item.id}
            columns={columns}
          >
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
          <Pagination
            pageCount={pageData.length}
            page={page}
            itemsPerPage={itemsPerPage}
            totalItemCount={data.length}
            setPage={setPage}
          />
        </>
      )}
    </TableWrapper>
  );
}

function filterData(item: BackupsClient, search: string) {
  const { name } = item;

  const searchableFields = {
    name,
    lastbackup: formatter.lastbackup(item),
  };

  return filterBySearch(search, searchableFields);
}
