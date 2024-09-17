import { useMutation, useQueryClient } from "@tanstack/react-query";
import {
  Toast,
  ToastTitle,
  useToastController,
} from "@fluentui/react-components";

import { urbackupServer } from "../../App";

export function useRemoveClientsMutation() {
  const queryClient = useQueryClient();

  const { dispatchToast } = useToastController("toaster");

  return useMutation({
    mutationFn: urbackupServer.removeClients,
    onSuccess: async () => {
      await queryClient.invalidateQueries({
        queryKey: ["status"],
      });

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
