import { useMutation } from "react-query";
import { urbackupServer } from "../../App";

import {
  Toast,
  ToastTitle,
  useToastController,
} from "@fluentui/react-components";

export function useBackupMutation() {
  const { dispatchToast } = useToastController("toaster");

  return useMutation({
    mutationFn: ({
      id,
      type,
    }: {
      id: Parameters<typeof urbackupServer.startBackup>["0"];
      type: Parameters<typeof urbackupServer.startBackup>["1"];
    }) => urbackupServer.startBackup(id, type),
    onSuccess: () => {
      dispatchToast(
        <Toast>
          <ToastTitle>Started backup</ToastTitle>
        </Toast>,
        { intent: "success" },
      );
    },
    onError: () => {
      dispatchToast(
        <Toast>
          <ToastTitle>Failed to start a backup</ToastTitle>
        </Toast>,
        { intent: "error" },
      );
    },
  });
}
