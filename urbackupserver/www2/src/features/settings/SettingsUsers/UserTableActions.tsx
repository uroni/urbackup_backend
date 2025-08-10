import {
  Button,
  Dialog,
  DialogActions,
  DialogBody,
  DialogContent,
  DialogSurface,
  DialogTitle,
  DialogTrigger,
} from "@fluentui/react-components";

import { UserListItem } from "../../../api/urbackupserver";
import { useUsers } from "./useUsers";
import { addMessage, clearMessages } from "./messageStore";
import { useState } from "react";

export function UserTableActions(user: UserListItem) {
  const { removeUser, isRemovable } = useUsers();

  const handleSuccess = () => {
    clearMessages();
    addMessage("success", "Successfully removed user.");
  };

  const handleFailure = () => {
    clearMessages();
    addMessage("error", `Failed to remove user.`);
  };

  const handleRemove = () => {
    removeUser(user.id, {
      onError: handleFailure,
      onSuccess: handleSuccess,
    });
  };

  const [open, setOpen] = useState(false);

  return (
    <div className="cluster gutter-xs">
      <Button>Change rights</Button>
      <Button>Change password</Button>

      {isRemovable(user) && (
        <Dialog
          open={open}
          onOpenChange={(event, data) => {
            setOpen(data.open);
          }}
        >
          <DialogTrigger disableButtonEnhancement>
            <Button appearance="subtle">Remove</Button>
          </DialogTrigger>
          <DialogSurface
            style={{
              maxWidth: "50ch",
            }}
          >
            <DialogBody className="flow">
              <DialogTitle>Remove user</DialogTitle>
              <DialogContent>
                Are you sure you want to remove this user?
              </DialogContent>
              <DialogActions>
                <DialogTrigger disableButtonEnhancement>
                  <Button appearance="secondary">Cancel</Button>
                </DialogTrigger>
                <Button appearance="primary" onClick={handleRemove}>
                  Delete
                </Button>
              </DialogActions>
            </DialogBody>
          </DialogSurface>
        </Dialog>
      )}
    </div>
  );
}
