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

import type { ActivityItem } from "../../api/urbackupserver";
import {
  format_size,
  formatDatetime,
  formatDuration,
} from "../../utils/format";
import { getActionFromLastAct } from "./getActionFromLastAct";

export const columns: TableColumnDefinition<ActivityItem>[] = [
  createTableColumn<ActivityItem>({
    columnId: "id",
    renderHeaderCell: () => {
      return "Id";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.id}</TableCellLayout>;
    },
  }),
  createTableColumn<ActivityItem>({
    columnId: "name",
    renderHeaderCell: () => {
      return "Computer name";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.name}</TableCellLayout>;
    },
  }),
  createTableColumn<ActivityItem>({
    columnId: "action",
    renderHeaderCell: () => {
      return "Action";
    },
    renderCell: (item) => {
      return <TableCellLayout>{getActionFromLastAct(item)}</TableCellLayout>;
    },
  }),
  createTableColumn<ActivityItem>({
    columnId: "details",
    renderHeaderCell: () => {
      return "Details";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.details || "-"}</TableCellLayout>;
    },
  }),
  createTableColumn<ActivityItem>({
    columnId: "backuptime",
    renderHeaderCell: () => {
      return "Starting time";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>{formatDatetime(item.backuptime)}</TableCellLayout>
      );
    },
  }),
  createTableColumn<ActivityItem>({
    columnId: "duration",
    renderHeaderCell: () => {
      return "Required time";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatDuration(item.duration)}</TableCellLayout>;
    },
  }),
  createTableColumn<ActivityItem>({
    columnId: "size_bytes",
    renderHeaderCell: () => {
      return "Used Storage";
    },
    renderCell: (item) => {
      return <TableCellLayout>{format_size(item.size_bytes)}</TableCellLayout>;
    },
  }),
];

export function LastActivitiesTable({
  data,
}: {
  data: ActivityItem[] | undefined;
}) {
  if (!data || data.length === 0) {
    return <span>No recent activities</span>;
  }

  return (
    <DataGrid items={data} getRowId={(item) => item.id} columns={columns}>
      <DataGridHeader>
        <DataGridRow>
          {({ renderHeaderCell }) => (
            <DataGridHeaderCell>{renderHeaderCell()}</DataGridHeaderCell>
          )}
        </DataGridRow>
      </DataGridHeader>
      <DataGridBody<ActivityItem>>
        {({ item }) => (
          <DataGridRow<ActivityItem> key={item.id}>
            {({ renderCell }) => (
              <DataGridCell>{renderCell(item)}</DataGridCell>
            )}
          </DataGridRow>
        )}
      </DataGridBody>
    </DataGrid>
  );
}
