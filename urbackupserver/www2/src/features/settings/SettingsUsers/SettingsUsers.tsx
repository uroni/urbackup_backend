import { useSettings } from "../useSettings";
import { UsersTable } from "./UsersTable";
import { TableWrapper } from "../../../components/TableWrapper";
import { CreateUser } from "./CreateUser";
import { Banner } from "../../../components/Banner/Banner";

export function SettingsUsers() {
  const { settings } = useSettings();

  if (!settings) {
    return null;
  }

  return (
    <div className="flow centered">
      <div className="repel">
        <h1>Users</h1>
        <CreateUser />
      </div>

      <Banner />

      <TableWrapper>
        <UsersTable />
      </TableWrapper>
    </div>
  );
}
