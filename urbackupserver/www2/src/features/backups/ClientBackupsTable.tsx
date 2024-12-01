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
  Badge,
  tokens,
  PresenceBadge,
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
    // Negate the margin top and left values
    // to keep breadcrumbs aligned to initial top-left position
    marginInlineStart: "-7px",
    marginBlockStart: "-7px",
  },
});

export const columns: TableColumnDefinition<Backup>[] = [
  createTableColumn<Backup>({
    columnId: "backuptime",
    renderHeaderCell: () => {
      return "Backup time";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>{formatDatetime(item.backuptime)}</TableCellLayout>
      );
    },
  }),
  createTableColumn<Backup>({
    columnId: "id",
    renderHeaderCell: () => {
      return "Backup ID";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: tokens.spacingHorizontalS,
            }}
          >
            <span>{item.id}</span>
            {!!item.archive_timeout && (
              <Badge color="informative">
                Unarchives: {formatDatetime(item.archive_timeout)}
              </Badge>
            )}
          </div>
        </TableCellLayout>
      );
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
      return <TableCellLayout>{format_size(item.size_bytes)}</TableCellLayout>;
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

export function ClientBackupsTable() {
  const { clientId } = useParams();

  const backupsResult = useSuspenseQuery({
    queryKey: ["backups", Number(clientId)],
    queryFn: () => urbackupServer.getBackups(Number(clientId)),
  });

  const classes = useStyles();

  const { clientname, clientid, backups } = backupsResult.data!;

  const breadcrumbItems = makeBackupsBreadcrumbs({
    clientId: Number(clientId),
    clientName: clientname,
  });

  // Client doesn't exist
  if (!clientname.length && !backups.length) {
    throw new Error(`No such client with ID: ${clientId}`);
  }

  if (backups.length === 0) {
    return (
      <TableWrapper>
        <div className={classes.heading}>
          <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
        </div>
        <p>
          No Backup created for <strong>{clientname}</strong> yet.
        </p>
      </TableWrapper>
    );
  }

  return (
    <TableWrapper>
      <div className={classes.heading}>
        <Breadcrumbs items={breadcrumbItems} wrapper={"h3"} />
      </div>
      <DataGrid items={backups} getRowId={(item) => item.id} columns={columns}>
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
                const noneColumns = [
                  "backuptime",
                  "id",
                  "incremental",
                  "size",
                  "archived",
                  "actions",
                ];
                const isInteractive = ["archived", "actions"].includes(
                  String(columnId),
                );

                return (
                  <DataGridCell
                    focusMode={getCellFocusMode(columnId, {
                      group: ["actions"],
                      none: noneColumns,
                    })}
                    className={isInteractive ? "" : classes.cell}
                    style={getNarrowColumnStyles(columnId)}
                  >
                    {!isInteractive && (
                      <Link to={String(item.id)} className={classes.link}>
                        {renderCell(item)}
                      </Link>
                    )}
                    {isInteractive && renderCell({ ...item, clientid })}
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
