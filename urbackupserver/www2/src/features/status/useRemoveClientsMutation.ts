import { useMutation, useQueryClient } from "react-query";

import { urbackupServer } from "../../App";

export function useRemoveClientsMutation() {
  const queryClient = useQueryClient();

  return useMutation(urbackupServer.removeClients, {
    onSuccess: () => {
      queryClient.invalidateQueries("status");
    },
  });
}
