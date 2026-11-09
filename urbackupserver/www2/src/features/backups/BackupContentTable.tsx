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
  Button,
  TableColumnId,
  MessageBar,
  MessageBarBody,
} from "@fluentui/react-components";
import { useState } from "react";
import { useParams, useSearchParams } from "react-router-dom";
import { useSuspenseQuery } from "@tanstack/react-query";
import { Document20Filled, Folder20Filled } from "@fluentui/react-icons";

import { BackupsAccessError, type File } from "../../api/urbackupserver";
import { getCellFocusMode } from "../../utils/table";
import { format_size, formatDatetime } from "../../utils/format";
import { urbackupServer } from "../../App";
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

function createFormatter<T extends File>() {
  return {
    size: (d: T) => (d.size ? format_size(d.size) : ""),
    creat: (d: T) => (d.creat ? formatDatetime(d.creat) : "-"),
    mod: (d: T) => (d.mod ? formatDatetime(d.mod) : "-"),
    access: (d: T) => (d.access ? formatDatetime(d.access) : "-"),
  } as Record<keyof T, (d: T) => string>;
}

const formatter = createFormatter();

export const columns: TableColumnDefinition<File>[] = [
  createTableColumn<File>({
    columnId: "name",
    renderHeaderCell: () => {
      return "Name";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          <span className="cluster gutter-s">
            {item.dir ? (
              <Folder20Filled />
            ) : (
              <Document20Filled color="var(--colorBrandBackground)" />
            )}
            {item.name}
          </span>
        </TableCellLayout>
      );
    },
  }),
  createTableColumn<File>({
    columnId: "size",
    renderHeaderCell: () => {
      return "Size";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatter.size(item)}</TableCellLayout>;
    },
  }),
  createTableColumn<File>({
    columnId: "created",
    renderHeaderCell: () => {
      return "Created";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatter.creat(item)}</TableCellLayout>;
    },
  }),
  createTableColumn<File>({
    columnId: "mod",
    renderHeaderCell: () => {
      return "Last modified";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatter.mod(item)}</TableCellLayout>;
    },
  }),
  createTableColumn<File>({
    columnId: "access",
    renderHeaderCell: () => {
      return "Last accessed";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatter.access(item)}</TableCellLayout>;
    },
  }),
  createTableColumn<File>({
    columnId: "actions",
    renderHeaderCell: () => "",
    renderCell: () => {
      return <Button>List</Button>;
    },
  }),
];

const noneColumns = ["name", "size", "created", "mod", "access"];

export function BackupContentTable() {
  const { clientId, backupId } = useParams();
  const [searchParams, setSearchParams] = useSearchParams();

  const path = searchParams.get("path");

  const { data, error, isFetching } = useSuspenseQuery({
    queryKey: [
      "backups",
      Number(clientId),
      Number(backupId),
      // Create unique keys for each path to save path files in cache for quick load
      ...(path ?? "").split("/").filter(Boolean),
    ],
    queryFn: () =>
      urbackupServer.getFiles(Number(clientId), Number(backupId), path ?? "/"),
    retry: 1,
  });

  const { clientname, backuptime, files } = data;

  const [restoreMessage, setRestoreMessage] = useState<{
    intent: "success" | "error";
    text: string;
  } | null>(null);

  const folderPath = path ?? "/";

  // Restores the folder, or only its file or folder `name`
  const restore = async (name?: string) => {
    if (
      data.server_confirms_restore &&
      !window.confirm(
        "Are you sure you want to restore the selected files? Existing files will be overwritten. Files created within the selected folder since the backup will be deleted. When in doubt please cancel and run a file backup before proceeding.",
      )
    ) {
      return;
    }

    setRestoreMessage(null);

    try {
      await urbackupServer.restoreFiles(
        Number(clientId),
        Number(backupId),
        folderPath,
        name,
      );
      setRestoreMessage({
        intent: "success",
        text: `Started restoring ${name ?? "the folder"} to ${clientname}. The restore shows in the activities.`,
      });
    } catch (e) {
      setRestoreMessage({
        intent: "error",
        text:
          e instanceof BackupsAccessError && e.message === "client_not_online"
            ? `Restoring failed: ${clientname} is not online.`
            : "Restoring failed. See the server log for details.",
      });
    }
  };

  const tableColumns = columns.map((c) =>
    c.columnId === "actions" && data.can_restore
      ? {
          ...c,
          renderCell: (item: File) => (
            <div className="cluster gutter-s">
              {c.renderCell(item)}
              <Button onClick={() => restore(item.name)}>Restore</Button>
            </div>
          ),
        }
      : c,
  );

  const breadcrumbItems = makeBackupsBreadcrumbs({
    clientId: Number(clientId),
    backupId: Number(backupId),
    backuptime,
    clientName: clientname!,
    path,
  });

  if (error && !isFetching) {
    throw error;
  }

  if (files.length === 0) {
    return (
      <TableWrapper>
        <div className="heading-breadcrumbs">
          <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
        </div>
        <p>No files exist in this path.</p>
      </TableWrapper>
    );
  }

  const { setSearch, filteredItems } = useFilteredBySearch<File>(
    files,
    filterData,
  );

  const { itemsPerPage, setItemsPerPage, pageData, page, setPage } =
    usePagination(filteredItems);

  return (
    <TableWrapper>
      <div className="repel heading-breadcrumbs">
        <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
        <div className="cluster gutter-s">
          {data.can_restore && (
            <Button onClick={() => restore()}>Restore folder to client</Button>
          )}
          <Button
            appearance="primary"
            onClick={() => {
              if (folderPath) {
                location.href = urbackupServer.downloadZipURL(
                  Number(clientId),
                  Number(backupId),
                  folderPath,
                );
              }
            }}
          >
            Download Folder as ZIP
          </Button>
        </div>
      </div>
      {restoreMessage && (
        <MessageBar intent={restoreMessage.intent}>
          <MessageBarBody>{restoreMessage.text}</MessageBarBody>
        </MessageBar>
      )}
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
            columns={tableColumns}
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
            <DataGridBody<File>>
              {({ item }) => (
                <DataGridRow<File>>
                  {({ renderCell, columnId }) => {
                    const isInteractive = ["actions"].includes(
                      String(columnId),
                    );

                    return (
                      <DataGridCell
                        focusMode={getCellFocusMode(columnId, {
                          group: ["actions"],
                          none: noneColumns,
                        })}
                        style={getNarrowColumnStyles(columnId)}
                      >
                        {isInteractive ? (
                          renderCell(item)
                        ) : (
                          <a
                            href=""
                            onClick={(e) => {
                              e.preventDefault();

                              // Navigate to new path if item is a directory
                              if (item.dir) {
                                const params = {
                                  path: `${path ?? ""}/${item.name}`,
                                };

                                setSearchParams(params);

                                return;
                              }

                              if (path) {
                                const filePath = `${path}/${item.name}`;
                                location.href = urbackupServer.downloadFileURL(
                                  Number(clientId),
                                  Number(backupId),
                                  filePath,
                                );
                              }
                            }}
                          >
                            {renderCell(item)}
                          </a>
                        )}
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
    </TableWrapper>
  );
}

const narrowColumnIds = ["size", "created", "mod", "access", "actions"];

/**
 * Style some columns to take up less space.
 */
function getNarrowColumnStyles(columnId: TableColumnId) {
  const stringId = columnId.toString();

  return {
    flexGrow: narrowColumnIds.includes(stringId) ? "0" : "1",
    flexBasis: narrowColumnIds.includes(stringId) ? "17ch" : "0",
  };
}

function filterData<T extends File>(item: T, search: string) {
  const { name } = item;

  const searchableFields = {
    name,
    size: formatter.size(item),
    creat: formatter.creat(item),
    mod: formatter.mod(item),
    access: formatter.access(item),
  } as Record<keyof T, string>;

  return filterBySearch(search, searchableFields);
}
