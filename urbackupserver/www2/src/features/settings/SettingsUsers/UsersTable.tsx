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
  TableColumnId,
  Body2,
} from "@fluentui/react-components";

import { UserListItem, UserRight } from "../../../api/urbackupserver";
import { getCellFocusMode } from "../../../utils/table";
import { UserTableActions } from "./UserTableActions";
import { useUsers, isUserAdmin } from "./useUsers";

export const columns: TableColumnDefinition<UserListItem>[] = [
  createTableColumn<UserListItem>({
    columnId: "username",
    renderHeaderCell: () => {
      return "Username";
    },
    renderCell: (item) => {
      return <TableCellLayout>{item.name}</TableCellLayout>;
    },
  }),
  createTableColumn<UserListItem>({
    columnId: "rights",
    renderHeaderCell: () => {
      return "Rights";
    },
    renderCell: (item) => {
      return <TableCellLayout>{getDisplayRights(item.rights)}</TableCellLayout>;
    },
  }),
  createTableColumn<UserListItem>({
    columnId: "actions",
    renderHeaderCell: () => {
      return "Actions";
    },
    renderCell: UserTableActions,
  }),
];

export function UsersTable() {
  const { users } = useUsers();

  if (users.length === 0) {
    return (
      <div>
        <Body2>No users</Body2>
      </div>
    );
  }

  return (
    <DataGrid items={users} getRowId={(item) => item.id} columns={columns}>
      <DataGridHeader>
        <DataGridRow>
          {({ renderHeaderCell, columnId }) => (
            <DataGridHeaderCell style={getNarrowColumnStyles(columnId)}>
              {renderHeaderCell()}
            </DataGridHeaderCell>
          )}
        </DataGridRow>
      </DataGridHeader>
      <DataGridBody<UserListItem>>
        {({ item }) => (
          <DataGridRow<UserListItem> key={item.id}>
            {({ renderCell, columnId }) => (
              <DataGridCell
                focusMode={getCellFocusMode(columnId)}
                style={getNarrowColumnStyles(columnId)}
              >
                {renderCell(item)}
              </DataGridCell>
            )}
          </DataGridRow>
        )}
      </DataGridBody>
    </DataGrid>
  );
}

const BASE_COL_WIDTH = {
  actions: "48ch",
} as const;

/**
 * Style some columns to take up less space.
 */
function getNarrowColumnStyles(columnId: TableColumnId) {
  const stringId = columnId.toString();

  if (!Object.keys(BASE_COL_WIDTH).includes(stringId)) {
    return;
  }

  return {
    flexGrow: "0",
    flexBasis: BASE_COL_WIDTH[stringId as keyof typeof BASE_COL_WIDTH],
  };
}

function getDisplayRights(rights: UserRight[]) {
  const isAdmin = isUserAdmin(rights);

  return isAdmin ? "Administrator" : "User";
}
