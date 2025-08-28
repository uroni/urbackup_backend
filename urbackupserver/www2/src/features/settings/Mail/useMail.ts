import {
  useMutation,
  useQueryClient,
  useSuspenseQuery,
} from "@tanstack/react-query";

import type {
  MailSettings,
  MailSettingsVals,
} from "../../../api/urbackupserver";
import { urbackupServer } from "../../../App";
import {
  addMessage,
  clearMessages,
  Message,
} from "../../../components/Banner/messageStore";

const QUERY_KEY = ["settings", "mail"];

export function useMail() {
  const queryClient = useQueryClient();

  const {
    data: { settings },
  } = useSuspenseQuery({
    queryKey: QUERY_KEY,
    queryFn: urbackupServer.getMailSettings,
  });

  const mutation = useMutation({
    mutationFn: ({
      settings,
      testmailaddr,
    }: {
      settings: MailSettingsVals;
      testmailaddr?: string;
    }) => urbackupServer.saveMailSettings(settings, testmailaddr),
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

  const handleSubmit = (settings: MailSettingsVals) => {
    mutation.mutate(
      { settings },
      {
        onError: () => handleFailure("Failed to save settings."),
        onSuccess: () => handleSuccess("Saved settings successfully."),
      },
    );
  };

  const sendTestMail = (
    testmailaddr: string,
    mutationOptions: {
      onError: (e: Error) => void;
      onSuccess: (data: MailSettings) => void;
    },
  ) => {
    mutation.mutate({ settings, testmailaddr }, mutationOptions);
  };

  return {
    settings,
    handleSubmit,
    sendTestMail,
  };
}
