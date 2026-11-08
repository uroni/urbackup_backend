import {
  Toast,
  ToastTitle,
  useToastController,
} from "@fluentui/react-components";
import { BackupType, StatusClientItem } from "../../api/urbackupserver";
import { useBackupMutation } from "./useBackupMutation";
import { useRemoveClientsMutation } from "./useRemoveClientsMutation";
import { useBackupResult } from "./BackupResultContext";

export function useStatusClientActions() {
  const { updateBackupResults } = useBackupResult();

  const removeClientsMutation = useRemoveClientsMutation();
  const backupMutation = useBackupMutation();

  const { dispatchToast } = useToastController("toaster");

  const removeClients = (idList: StatusClientItem["id"][]) => {
    if (idList.length) {
      removeClientsMutation.mutate(idList);
    }
  };

  const startBackup = (type: BackupType, idList: StatusClientItem["id"][]) => {
    if (idList.length) {
      backupMutation.mutate(
        { id: idList, type },
        {
          onSuccess: (data) => {
            updateBackupResults(data.result);

            const failedBackups = data.result.filter((r) => !r.start_ok);

            if (failedBackups.length) {
              return dispatchToast(
                <Toast>
                  <ToastTitle>Starting some backups failed</ToastTitle>
                </Toast>,
                { intent: "error" },
              );
            }

            dispatchToast(
              <Toast>
                <ToastTitle>Started backup</ToastTitle>
              </Toast>,
              { intent: "success" },
            );
          },
        },
      );
    }
  };

  return {
    removeClients,
    startBackup,
  };
}
