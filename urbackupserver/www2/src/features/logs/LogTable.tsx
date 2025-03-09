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
  tokens,
  TableColumnId,
} from "@fluentui/react-components";
import {
  DismissCircle16Filled,
  Info16Filled,
  Warning16Filled,
} from "@fluentui/react-icons";

import { LOG_LEVELS, type LogDataRow } from "../../api/urbackupserver";
import { formatDatetime } from "../../utils/format";

export const FORMATTED_LOG_TABLE_LEVELS = {
  INFO: "Info",
  WARNING: "Warnings",
  ERROR: "Errors",
} as const;

const LOG_LEVELS_MAP = new Map(
  (Object.keys(LOG_LEVELS) as (keyof typeof LOG_LEVELS)[]).map((k) => [
    LOG_LEVELS[k],
    k,
  ]),
);

const columns: TableColumnDefinition<LogDataRow>[] = [
  createTableColumn<LogDataRow>({
    columnId: "level",
    renderHeaderCell: () => {
      return "Level";
    },
    renderCell: (item) => {
      return (
        <TableCellLayout>
          <div className="cluster" data-spacing="s">
            {LOG_LEVELS_MAP.get(item.level) === "INFO" && (
              <Info16Filled
                style={{
                  color: tokens.colorBrandForeground1,
                }}
              />
            )}
            {LOG_LEVELS_MAP.get(item.level) === "WARNING" && (
              <Warning16Filled
                style={{
                  color: tokens.colorStatusWarningForeground1,
                }}
              />
            )}
            {LOG_LEVELS_MAP.get(item.level) === "ERROR" && (
              <DismissCircle16Filled
                style={{
                  color: tokens.colorStatusDangerForeground1,
                }}
              />
            )}
            {FORMATTED_LOG_TABLE_LEVELS[LOG_LEVELS_MAP.get(item.level)!]}
          </div>
        </TableCellLayout>
      );
    },
  }),
  createTableColumn<LogDataRow>({
    columnId: "time",
    renderHeaderCell: () => {
      return "Time";
    },
    renderCell: (item) => {
      return <TableCellLayout>{formatDatetime(item.time)}</TableCellLayout>;
    },
  }),
  createTableColumn<LogDataRow>({
    columnId: "message",
    renderHeaderCell: () => {
      return "Message";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.message}</TableCellLayout>;
    },
  }),
];

export function LogTable({ data }: { data: LogDataRow[] }) {
  return (
    <DataGrid items={data} getRowId={(item) => item.id} columns={columns}>
      <DataGridHeader>
        <DataGridRow>
          {({ renderHeaderCell, columnId }) => (
            <DataGridHeaderCell style={getNarrowColumnStyles(columnId)}>
              {renderHeaderCell()}
            </DataGridHeaderCell>
          )}
        </DataGridRow>
      </DataGridHeader>
      <DataGridBody<LogDataRow>>
        {({ item }) => (
          <DataGridRow<LogDataRow>>
            {({ renderCell, columnId }) => (
              <DataGridCell style={getNarrowColumnStyles(columnId)}>
                {renderCell(item)}
              </DataGridCell>
            )}
          </DataGridRow>
        )}
      </DataGridBody>
    </DataGrid>
  );
}

/**
 * Style some columns to take up less space.
 */
function getNarrowColumnStyles(columnId: TableColumnId) {
  const stringId = columnId.toString();

  const widths = {
    level: "12ch",
    time: "20ch",
  } as const;

  const widthKeys = Object.keys(widths);

  return {
    flexGrow: widthKeys.includes(stringId) ? "0" : "1",
    flexBasis: widthKeys.includes(stringId)
      ? widths[stringId as keyof typeof widths]
      : "0",
  };
}
