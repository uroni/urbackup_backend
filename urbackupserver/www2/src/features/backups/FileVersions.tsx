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
} from "@fluentui/react-components";
import { Link, useParams, useSearchParams } from "react-router-dom";
import { useSuspenseQuery } from "@tanstack/react-query";

import type { File } from "../../api/urbackupserver";
import { format_size, formatDatetime } from "../../utils/format";
import { urbackupServer } from "../../App";
import { BASE_HREF, Breadcrumbs } from "../../components/Breadcrumbs";
import { makeBackupsBreadcrumbs } from "./makeBackupsBreadcrumbs";
import { TableWrapper } from "../../components/TableWrapper";

/**
 * One path (file or folder) in every file backup of a client that contains it.
 */
export function FileVersions() {
  const { clientId } = useParams();
  const [searchParams] = useSearchParams();
  const path = searchParams.get("path") ?? "/";
  const parent = path.slice(0, path.lastIndexOf("/"));

  const { data } = useSuspenseQuery({
    queryKey: ["backups", Number(clientId), "versions", path],
    queryFn: () => urbackupServer.getFileVersions(Number(clientId), path),
  });

  const breadcrumbItems = [
    ...makeBackupsBreadcrumbs({
      clientId: Number(clientId),
      clientName: data.clientname!,
    }),
    {
      key: "versions",
      text: path,
      itemProps: {
        href: `${BASE_HREF}/backups/${clientId}/versions?${searchParams}`,
      },
    },
  ];

  const columns: TableColumnDefinition<File>[] = [
    createTableColumn<File>({
      columnId: "backuptime",
      renderHeaderCell: () => "Backup time",
      renderCell: (item) => (
        <TableCellLayout>
          <Link
            to={`/backups/${clientId}/${item.backupid}?${new URLSearchParams({ path: parent })}`}
          >
            {formatDatetime(item.backuptime!)}
          </Link>
        </TableCellLayout>
      ),
    }),
    createTableColumn<File>({
      columnId: "size",
      renderHeaderCell: () => "Size",
      renderCell: (item) => (
        <TableCellLayout>
          {item.dir ? "Folder" : format_size(item.size ?? 0)}
        </TableCellLayout>
      ),
    }),
    createTableColumn<File>({
      columnId: "mod",
      renderHeaderCell: () => "Last modified",
      renderCell: (item) => (
        <TableCellLayout>
          {item.mod ? formatDatetime(item.mod) : "-"}
        </TableCellLayout>
      ),
    }),
    createTableColumn<File>({
      columnId: "actions",
      renderHeaderCell: () => "",
      renderCell: (item) => (
        <Button
          onClick={() => {
            location.href = item.dir
              ? urbackupServer.downloadZipURL(
                  Number(clientId),
                  item.backupid!,
                  path,
                )
              : urbackupServer.downloadFileURL(
                  Number(clientId),
                  item.backupid!,
                  path,
                );
          }}
        >
          {item.dir ? "Download as ZIP" : "Download"}
        </Button>
      ),
    }),
  ];

  return (
    <TableWrapper>
      <div className="heading-breadcrumbs">
        <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
      </div>
      {data.files.length === 0 ? (
        <p>This path is not in any file backup.</p>
      ) : (
        <DataGrid
          items={data.files}
          getRowId={(item) => item.backupid}
          columns={columns}
        >
          <DataGridHeader>
            <DataGridRow>
              {({ renderHeaderCell }) => (
                <DataGridHeaderCell>{renderHeaderCell()}</DataGridHeaderCell>
              )}
            </DataGridRow>
          </DataGridHeader>
          <DataGridBody<File>>
            {({ item }) => (
              <DataGridRow<File>>
                {({ renderCell }) => (
                  <DataGridCell>{renderCell(item)}</DataGridCell>
                )}
              </DataGridRow>
            )}
          </DataGridBody>
        </DataGrid>
      )}
    </TableWrapper>
  );
}
