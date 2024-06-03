import { BackupType, StatusClientItem } from "../../api/urbackupserver";
import { useBackupMutation } from "./useBackupMutation";
import { useRemoveClientsMutation } from "./useRemoveClientsMutation";

export function useStatusClientActions() {
  const removeClientsMutation = useRemoveClientsMutation();
  const backupMutation = useBackupMutation();

  const removeClients = (idList: StatusClientItem["id"][]) => {
    if (idList.length) {
      removeClientsMutation.mutate(idList);
    }
  };

  const startBackup = (type: BackupType, idList: StatusClientItem["id"][]) => {
    if (idList.length) {
      backupMutation.mutate({ id: idList, type });
    }
  };

  return {
    removeClients,
    startBackup,
  };
}
