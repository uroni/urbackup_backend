import {
  TableColumnId,
  DataGridCellFocusMode,
} from "@fluentui/react-components";

export const getCellFocusMode = (
  columnId: TableColumnId,
  options: {
    group?: string[];
    none?: string[];
  } = {
    group: ["actions"],
    none: [],
  },
): DataGridCellFocusMode => {
  const { group, none } = options;

  if (group?.includes(String(columnId))) {
    return "group";
  }
  if (none?.includes(String(columnId))) {
    return "none";
  }

  return "cell";
};
