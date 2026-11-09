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
  Badge,
  PresenceBadge,
  Subtitle2,
  TableColumnId,
} from "@fluentui/react-components";
import { Link, useParams } from "react-router-dom";
import { useSuspenseQuery } from "@tanstack/react-query";

import type { Backup } from "../../api/urbackupserver";
import { getCellFocusMode } from "../../utils/table";
import { format_size, formatDatetime } from "../../utils/format";
import { urbackupServer } from "../../App";
import { ClientBackupActions } from "./ClientBackupActions";
import { ArchiveCheckbox } from "./ArchiveCheckbox";
import { Breadcrumbs } from "../../components/Breadcrumbs";
import { makeBackupsBreadcrumbs } from "./makeBackupsBreadcrumbs";
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

function createFormatter<T extends Backup>() {
  return {
    backuptime: (d: T) => formatDatetime(d.backuptime),
    archived: (d: T) => {
      if (d.archive_timeout) {
        return `archived: Unarchives ${formatDatetime(d.archive_timeout)}`;
      }

      return d.archived ? `archived` : "";
    },
    incremental: (d: T) => (d.incremental !== 0 ? `incremental` : ""),
    size_bytes: (d: T) => format_size(d.size_bytes),
  } as Record<keyof T, (d: T) => string>;
}

const formatter = createFormatter();

export const columns: TableColumnDefinition<Backup>[] = [
  createTableColumn<Backup>({
    columnId: "backuptime",
    renderHeaderCell: () => {
      return "Backup time";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatter.backuptime(item)}</TableCellLayout>;
    },
  }),
  createTableColumn<Backup>({
    columnId: "id",
    renderHeaderCell: () => {
      return "Backup ID";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.id}</TableCellLayout>;
    },
  }),
  createTableColumn<Backup>({
    columnId: "volume",
    renderHeaderCell: () => {
      return "Volume";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.letter}</TableCellLayout>;
    },
  }),
  createTableColumn<Backup>({
    columnId: "incremental",
    renderHeaderCell: () => {
      return "Incremental";
    },
    renderCell: (item) => {
      const isIncremental = item.incremental !== 0;

      return (
        <TableCellLayout>
          {isIncremental && <PresenceBadge status="available" />}
        </TableCellLayout>
      );
    },
  }),
  createTableColumn<Backup>({
    columnId: "size",
    renderHeaderCell: () => {
      return "Size";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatter.size_bytes(item)}</TableCellLayout>;
    },
  }),
  createTableColumn<Backup & { clientid: number }>({
    columnId: "archived",
    renderHeaderCell: () => {
      return "Archived";
    },
    renderCell: ArchiveCheckbox,
  }),
  createTableColumn<Backup & { clientid: number }>({
    columnId: "actions",
    renderHeaderCell: () => {
      return "Actions";
    },
    renderCell: ClientBackupActions,
  }),
];

const fileColumns = columns.filter((c) => c.columnId !== "volume");

// Like the old web interface: the volume instead of the id
const imageColumns = columns.filter((c) => c.columnId !== "id");

export function ClientBackupsTable() {
  const { clientId } = useParams();

  const backupsResult = useSuspenseQuery({
    queryKey: ["backups", Number(clientId)],
    queryFn: () => urbackupServer.getBackups(Number(clientId)),
  });

  const { clientname, clientid, backups } = backupsResult.data!;
  const backupImages = backupsResult.data!.backup_images ?? [];

  const breadcrumbItems = makeBackupsBreadcrumbs({
    clientId: Number(clientId),
    clientName: clientname,
  });

  // Client doesn't exist
  if (!clientname.length && !backups.length) {
    throw new Error(`No such client with ID: ${clientId}`);
  }

  return (
    <TableWrapper>
      <div className="heading-breadcrumbs">
        <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
      </div>
      {backups.length === 0 && backupImages.length === 0 ? (
        <p>
          No Backup created for <strong>{clientname}</strong> yet.
        </p>
      ) : (
        <>
          {backupImages.length > 0 && (
            <Subtitle2 as="h4">File backups</Subtitle2>
          )}
          {backups.length > 0 ? (
            <BackupsGrid
              items={backups}
              columns={fileColumns}
              clientid={clientid}
            />
          ) : (
            <p>No file backups yet.</p>
          )}
          {backupImages.length > 0 && (
            <>
              <Subtitle2 as="h4">Image backups</Subtitle2>
              {/* Image backups have negative ids, which open them as images */}
              <BackupsGrid
                items={backupImages}
                columns={imageColumns}
                clientid={clientid}
              />
            </>
          )}
        </>
      )}
    </TableWrapper>
  );
}

function BackupsGrid({
  items,
  columns,
  clientid,
}: {
  items: Backup[];
  columns: TableColumnDefinition<Backup>[];
  clientid: number;
}) {
  const { setSearch, filteredItems } = useFilteredBySearch<Backup>(
    items,
    filterData,
  );

  const { itemsPerPage, setItemsPerPage, pageData, page, setPage } =
    usePagination(filteredItems);

  const columnIds = columns.map((c) => String(c.columnId));

  return (
    <>
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
                {({ renderHeaderCell, columnId }) => (
                  <DataGridHeaderCell style={getNarrowColumnStyles(columnId)}>
                    {renderHeaderCell()}
                  </DataGridHeaderCell>
                )}
              </DataGridRow>
            </DataGridHeader>
            <DataGridBody<Backup>>
              {({ item }) => (
                <DataGridRow<Backup> key={item.id}>
                  {({ renderCell, columnId }) => {
                    const isInteractive = ["archived", "actions"].includes(
                      String(columnId),
                    );

                    return (
                      <DataGridCell
                        focusMode={getCellFocusMode(columnId, {
                          group: ["actions"],
                          none: columnIds,
                        })}
                        style={getNarrowColumnStyles(columnId)}
                      >
                        {!isInteractive && (
                          <Link to={String(item.id)}>{renderCell(item)}</Link>
                        )}
                        {isInteractive && renderCell({ ...item, clientid })}
                      </DataGridCell>
                    );
                  }}
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
    </>
  );
}

const narrowColumnIds = ["id", "incremental", "size"];

/**
 * Style some columns to take up less space.
 */
function getNarrowColumnStyles(columnId: TableColumnId) {
  const stringId = columnId.toString();

  const flexGrow = narrowColumnIds.includes(stringId) ? "0" : "1";

  if (["actions"].includes(stringId)) {
    return {
      flexGrow,
      flexBasis: "18ch",
    };
  }

  return {
    flexGrow,
    flexBasis: narrowColumnIds.includes(stringId) ? "14ch" : "0",
  };
}

function filterData<T extends Backup>(item: T, search: string) {
  const { id, letter } = item;

  const searchableFields = {
    id,
    letter,
    backuptime: formatter.backuptime(item),
    archived: formatter.archived(item),
    incremental: formatter.incremental(item),
    size_bytes: formatter.size_bytes(item),
  } as Record<keyof T, string>;

  return filterBySearch(search, searchableFields);
}
