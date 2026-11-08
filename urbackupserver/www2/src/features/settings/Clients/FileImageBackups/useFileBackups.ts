import {
  useMutation,
  useQueryClient,
  useSuspenseQuery,
} from "@tanstack/react-query";

import type {
  ClientSettings,
  GeneralSettings,
} from "../../../../api/urbackupserver";
import { urbackupServer } from "../../../../App";
import {
  addMessage,
  clearMessages,
} from "../../../../components/Banner/messageStore";

const QUERY_KEY = ["settings", "clients", "backups"];

// Settings of the client id 0 are the general settings
export const GENERAL_SETTINGS = 0;

/**
 * The general settings in the form of client settings that are set "here"
 */
export function asClientSettings({ settings }: GeneralSettings) {
  return {
    settings: Object.fromEntries(
      Object.entries(settings).map(([k, v]) => {
        // Settings of clients arrive as { value }, the server settings plain
        if (v === null || typeof v !== "object") {
          return [k, { use: 2, value: v }];
        }

        return [k, "value" in v ? { ...v, use: 2 } : v];
      }),
    ),
  } as ClientSettings;
}

export function useFileBackups(clientId: number) {
  const queryClient = useQueryClient();

  const queryKey = [...QUERY_KEY, Number(clientId)];

  const {
    data: { settings },
  } = useSuspenseQuery({
    queryKey,
    queryFn: () =>
      clientId === GENERAL_SETTINGS
        ? urbackupServer.getGeneralSettings().then(asClientSettings)
        : urbackupServer.getClientSettings(clientId),
  });

  const mutation = useMutation({
    mutationFn: ({
      settings,
    }: {
      settings: Parameters<typeof urbackupServer.saveClientSettings>[1];
    }): Promise<unknown> =>
      clientId === GENERAL_SETTINGS
        ? urbackupServer.saveGeneralSettings(
            settings as Parameters<
              typeof urbackupServer.saveGeneralSettings
            >[0],
          )
        : urbackupServer.saveClientSettings(clientId, settings),
    onSuccess: () => {
      return queryClient.invalidateQueries({
        // The server page shows the general settings too
        queryKey: clientId === GENERAL_SETTINGS ? ["settings"] : queryKey,
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
