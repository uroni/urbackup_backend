import { MenuItem } from "@fluentui/react-components";
import { useMutation, useQueryClient } from "react-query";

import { urbackupServer } from "../../App";
import { StatusClientItem } from "../../api/urbackupserver";
import { StatusMenu } from "./StatusMenu";
import { useBackupMutation } from "./useBackupMutation";

export function StatusMenuGrid({
  idList,
}: {
  idList: StatusClientItem["id"][];
}) {
  const queryClient = useQueryClient();

  const removeClientMutation = useMutation(urbackupServer.removeClients, {
    onSuccess: () => {
      queryClient.invalidateQueries("status");
    },
  });

  const backupMutation = useBackupMutation();

  const removeClients = () => {
    removeClientMutation.mutate(idList);
  };

  const startBackup = (e: React.MouseEvent<HTMLDivElement>) => {
    const type = e.currentTarget.dataset.type;

    if (type) {
      backupMutation.mutate({ id: idList, type: +type });
    }
  };

  return (
    <StatusMenu onBackup={startBackup}>
      <MenuItem onClick={removeClients}>Remove clients</MenuItem>
    </StatusMenu>
  );
}
