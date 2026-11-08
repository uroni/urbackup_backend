import {
  useMutation,
  useQueryClient,
  useSuspenseQuery,
} from "@tanstack/react-query";

import type { LdapSettingsVals } from "../../../api/urbackupserver";
import { urbackupServer } from "../../../App";
import {
  addMessage,
  clearMessages,
} from "../../../components/Banner/messageStore";

const QUERY_KEY = ["settings", "ldap"];

export function useLdapAd() {
  const queryClient = useQueryClient();

  const {
    data: { settings },
  } = useSuspenseQuery({
    queryKey: QUERY_KEY,
    queryFn: urbackupServer.getLdapSettings,
  });

  const mutation = useMutation({
    mutationFn: ({ settings }: { settings: LdapSettingsVals }) =>
      urbackupServer.saveLdapSettings(settings),
    onSuccess: () => {
      return queryClient.invalidateQueries({
        queryKey: QUERY_KEY,
      });
    },
  });

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

  const handleSubmit = (settings: LdapSettingsVals) => {
    mutation.mutate(
      { settings },
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
