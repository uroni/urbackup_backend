import {
  useSuspenseQuery,
  useMutation,
  useQueryClient,
} from "@tanstack/react-query";

import { urbackupServer } from "../../../App";
import type { UserListItem, UserRight } from "../../../api/urbackupserver";

export interface UserInput {
  username: string;
  password: string;
  rights: UserRight[];
}

export function useUsers() {
  const queryClient = useQueryClient();

  const invalidateQueries = () => {
    return queryClient.invalidateQueries({
      queryKey: ["users"],
    });
  };

  const { data } = useSuspenseQuery({
    queryKey: ["users"],
    queryFn: urbackupServer.getUserList,
  });

  const isRemovable = (user: UserListItem) => {
    if (!isUserAdmin(user.rights)) {
      return true;
    }

    // Prevent deleting the last admin before others users are removed
    const admins = data.users.filter((u) => isUserAdmin(u.rights));
    const hasSingleAdmin = data.users.length > 1 && admins.length === 1;

    if (hasSingleAdmin) {
      return false;
    }

    return true;
  };

  const createUserMutation = useMutation({
    mutationFn: ({
      username,
      password,
      rights,
    }: {
      username: string;
      password: string;
      rights: UserRight[];
    }) => urbackupServer.createUser(username, password, rights),
    onSuccess: invalidateQueries,
  });

  const createUser = (
    user: UserInput,
    mutationOptions: {
      onError: (e: Error) => void;
      onSuccess: () => void;
    },
  ) => {
    createUserMutation.mutate(user, mutationOptions);
  };

  const removeUserMutation = useMutation({
    mutationFn: urbackupServer.removeUser,
    onSuccess: invalidateQueries,
  });

  const removeUser = (
    userId: string,
    mutationOptions: {
      onError: (e?: Error) => void;
      onSuccess: () => void;
    },
  ) => {
    removeUserMutation.mutate(userId, mutationOptions);
  };

  return {
    users: data.users,
    createUser,
    isRemovable,
    removeUser,
  };
}

export function isUserAdmin(rights: UserRight[]) {
  return rights.some((r) => r.domain === "all" && r.right === "all");
}
