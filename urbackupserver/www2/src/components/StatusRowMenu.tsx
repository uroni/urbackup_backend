import {
  Menu,
  MenuButton,
  MenuItem,
  MenuList,
  MenuPopover,
  MenuTrigger,
} from "@fluentui/react-components";
import { MoreHorizontal24Filled } from "@fluentui/react-icons";
import { StatusClientItem } from "../api/urbackupserver";
import { useMutation, useQueryClient } from "react-query";
import { urbackupServer } from "../App";

export function StatusRowMenu({ id }: { id: StatusClientItem["id"] }) {
  const queryClient = useQueryClient();

  const removeClientMutation = useMutation(urbackupServer.removeClient, {
    onSuccess: () => {
      queryClient.invalidateQueries("status");
    },
  });

  const removeClient = () => {
    removeClientMutation.mutate(id);
  };

  return (
    <Menu>
      <MenuTrigger disableButtonEnhancement>
        <MenuButton
          appearance="transparent"
          aria-label="More"
          icon={<MoreHorizontal24Filled />}
          onClick={(e) => e.stopPropagation()}
        />
      </MenuTrigger>

      <MenuPopover onClick={(e) => e.stopPropagation()}>
        <MenuList>
          <MenuItem onClick={removeClient}>Remove client</MenuItem>
        </MenuList>
      </MenuPopover>
    </Menu>
  );
}
