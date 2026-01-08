import {
  useMutation,
  useQueryClient,
  useSuspenseQuery,
} from "@tanstack/react-query";

import type { ClientSettings } from "../../../../api/urbackupserver";
import { urbackupServer } from "../../../../App";
import {
  addMessage,
  clearMessages,
} from "../../../../components/Banner/messageStore";

const QUERY_KEY = ["settings", "clients", "backups"];

export function useFileBackups(clientId: number) {
  const queryClient = useQueryClient();

  const queryKey = [...QUERY_KEY, Number(clientId)];

  const {
    data: { settings },
  } = useSuspenseQuery({
    queryKey,
    queryFn: () => urbackupServer.getClientSettings(clientId),
  });

  const mutation = useMutation({
    mutationFn: ({
      settings,
    }: {
      settings: Parameters<typeof urbackupServer.saveClientSettings>[1];
    }) => urbackupServer.saveClientSettings(clientId, settings),
    onSuccess: () => {
      return queryClient.invalidateQueries({
        queryKey,
      });
    },
  });

  // TODO: Converge the common functions between the other $useForm hooks
  const handleSuccess = (text: string, title?: string) => {
    clearMessages();
    addMessage({
      intent: "success",
      text,
      title,
    });
  };

  const handleFailure = (text: string, title?: string) => {
    clearMessages();
    addMessage({
      intent: "error",
      text,
      title,
    });
  };

  const handleSubmit = (settings: ClientSettings["settings"]) => {
    mutation.mutate(
      {
        settings: {
          settings,
        },
      },
      {
        onError: () => handleFailure("Failed to save settings."),
        onSuccess: () => handleSuccess("Saved settings successfully."),
      },
    );
  };

  return {
    settings,
    handleSubmit,
  };
}
