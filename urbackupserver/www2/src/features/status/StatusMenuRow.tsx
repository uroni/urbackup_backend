import { MenuItem } from "@fluentui/react-components";
import { useMutation, useQueryClient } from "react-query";

import { urbackupServer } from "../../App";
import { StatusClientItem } from "../../api/urbackupserver";
import { StatusMenu } from "./StatusMenu";
import { useBackupMutation } from "./useBackupMutation";

export function StatusMenuRow({ id }: { id: StatusClientItem["id"] }) {
  const queryClient = useQueryClient();

  const removeClientMutation = useMutation(urbackupServer.removeClient, {
    onSuccess: () => {
      queryClient.invalidateQueries("status");
    },
  });

  const backupMutation = useBackupMutation();

  const removeClient = () => {
    removeClientMutation.mutate(id);
  };

  const startBackup = (e: React.MouseEvent<HTMLDivElement>) => {
    const type = e.currentTarget.dataset.type;

    if (type) {
      backupMutation.mutate({ id: [id], type: +type });
    }
  };

  return (
    <StatusMenu onBackup={startBackup}>
      <MenuItem onClick={removeClient}>Remove client</MenuItem>
    </StatusMenu>
  );
}
