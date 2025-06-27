import {
  useMutation,
  useQueryClient,
  useSuspenseQuery,
} from "@tanstack/react-query";

import { urbackupServer } from "../../App";
import type { GeneralSettings, SettingState } from "../../api/urbackupserver";

const QUERY_KEY = ["settings"];

export function useSettings() {
  const queryClient = useQueryClient();

  const {
    data: { settings, navitems },
  } = useSuspenseQuery({
    queryKey: QUERY_KEY,
    queryFn: urbackupServer.getGeneralSettings,
  });

  const mutation = useMutation({
    mutationFn: urbackupServer.saveGeneralSettings,
    onSuccess: () => {
      return queryClient.invalidateQueries({
        queryKey: QUERY_KEY,
      });
    },
  });

  const updateSettings = (settings: GeneralSettings["settings"]) => {
    mutation.mutate({ settings });
  };

  return {
    settings: settings as Record<string, SettingState["value"]>,
    navitems,
    updateSettings,
  };
}
