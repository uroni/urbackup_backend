import { BackupType, StatusClientItem } from "../../api/urbackupserver";
import { useBackupMutation } from "./useBackupMutation";
import { useRemoveClientsMutation } from "./useRemoveClientsMutation";

export function useStatusClientActions() {
  const removeClientsMutation = useRemoveClientsMutation();
  const backupMutation = useBackupMutation();

  const removeClients = (idList: StatusClientItem["id"][]) => {
    removeClientsMutation.mutate(idList);
  };

  const startBackup = (type: BackupType, idList: StatusClientItem["id"][]) => {
    backupMutation.mutate({ id: idList, type });
  };

  return {
    removeClients,
    startBackup,
  };
}
