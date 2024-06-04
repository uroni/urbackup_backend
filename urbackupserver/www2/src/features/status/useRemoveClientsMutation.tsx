import { useMutation, useQueryClient } from "react-query";
import {
  Toast,
  ToastTitle,
  useToastController,
} from "@fluentui/react-components";

import { urbackupServer } from "../../App";

export function useRemoveClientsMutation() {
  const queryClient = useQueryClient();

  const { dispatchToast } = useToastController("toaster");

  return useMutation(urbackupServer.removeClients, {
    onSuccess: () => {
      queryClient.invalidateQueries("status");

      dispatchToast(
        <Toast>
          <ToastTitle>Scheduled client removal</ToastTitle>
        </Toast>,
        { intent: "success" },
      );
    },
    onError: () => {
      dispatchToast(
        <Toast>
          <ToastTitle>Failed to remove client</ToastTitle>
        </Toast>,
        { intent: "error" },
      );
    },
  });
}
