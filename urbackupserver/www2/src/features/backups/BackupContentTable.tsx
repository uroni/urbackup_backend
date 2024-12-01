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
  makeStyles,
  Button,
  TableColumnId,
  tokens,
} from "@fluentui/react-components";
import { useParams, useSearchParams } from "react-router-dom";
import { useSuspenseQuery } from "@tanstack/react-query";
import { Document20Filled, Folder20Filled } from "@fluentui/react-icons";

import type { File } from "../../api/urbackupserver";
import { getCellFocusMode } from "../../utils/table";
import { format_size, formatDatetime } from "../../utils/format";
import { urbackupServer } from "../../App";
import { Breadcrumbs } from "../../components/Breadcrumbs";
import { makeBackupsBreadcrumbs } from "./makeBackupsBreadcrumbs";
import { TableWrapper } from "../../components/TableWrapper";

const useStyles = makeStyles({
  cell: {
    alignItems: "unset",
  },
  link: {
    color: "inherit",
    textDecoration: "none",
    display: "flex",
    alignItems: "center",
    width: "100%",
  },
  heading: {
    display: "flex",
    gap: tokens.spacingHorizontalM,
    // Negate the margin top and left values
    // to keep breadcrumbs aligned to initial top-left position
    marginInlineStart: "-7px",
    marginBlockStart: "-7px",
  },
  headingAction: {
    marginInlineStart: "auto",
  },
});

const tableStyles = {
  name: {
    display: "flex",
    gap: tokens.spacingHorizontalS,
    alignItems: "center",
  },
  file: {
    color: tokens.colorBrandBackground,
  },
};

export const columns: TableColumnDefinition<File>[] = [
  createTableColumn<File>({
    columnId: "name",
    renderHeaderCell: () => {
      return "Name";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          <span style={tableStyles.name}>
            {item.dir ? (
              <Folder20Filled />
            ) : (
              <Document20Filled style={tableStyles.file} />
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
      return (
        <TableCellLayout>
          {item.size ? format_size(item.size) : ""}
        </TableCellLayout>
      );
    },
  }),
  createTableColumn<File>({
    columnId: "created",
    renderHeaderCell: () => {
      return "Created";
    },
    renderCell: (item) => {
      if (!item.creat) {
        return "-";
      }

      return <TableCellLayout>{formatDatetime(item.creat)}</TableCellLayout>;
    },
  }),
  createTableColumn<File>({
    columnId: "mod",
    renderHeaderCell: () => {
      return "Last modified";
    },
    renderCell: (item) => {
      if (!item.mod) {
        return "-";
      }

      return <TableCellLayout>{formatDatetime(item.mod)}</TableCellLayout>;
    },
  }),
  createTableColumn<File>({
    columnId: "access",
    renderHeaderCell: () => {
      return "Last accessed";
    },
    renderCell: (item) => {
      if (!item.access) {
        return "-";
      }

      return <TableCellLayout>{formatDatetime(item.access)}</TableCellLayout>;
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

  const classes = useStyles();

  const { clientname, backuptime, files } = data;

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
        <div className={classes.heading}>
          <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
        </div>
        <p>No files exist in this path.</p>
      </TableWrapper>
    );
  }

  return (
    <TableWrapper>
      <div className={classes.heading}>
        <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
        <Button
          appearance="primary"
          className={classes.headingAction}
          onClick={() => {
            const folderPath = path ?? "/";
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
      <DataGrid items={files} getRowId={(item) => item.id} columns={columns}>
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
                const isInteractive = ["actions"].includes(String(columnId));

                return (
                  <DataGridCell
                    focusMode={getCellFocusMode(columnId, {
                      group: ["actions"],
                      none: noneColumns,
                    })}
                    className={isInteractive ? "" : classes.cell}
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
                        className={classes.link}
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
