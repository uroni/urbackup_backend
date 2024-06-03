import { MenuItem } from "@fluentui/react-components";

import { StatusClientItem } from "../../api/urbackupserver";
import { StatusMenu } from "./StatusMenu";
import { useStatusClientActions } from "./useStatusClientActions";

export function StatusMenuGrid({
  idList,
}: {
  idList: StatusClientItem["id"][];
}) {
  const { removeClients, startBackup } = useStatusClientActions();

  return (
    <StatusMenu onBackup={(type) => startBackup(type, idList)}>
      <MenuItem onClick={() => removeClients(idList)}>Remove clients</MenuItem>
    </StatusMenu>
  );
}
