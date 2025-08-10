import { useEffect } from "react";

import { useSettings } from "../useSettings";
import { UsersTable } from "./UsersTable";
import { TableWrapper } from "../../../components/TableWrapper";
import {
  Button,
  MessageBar,
  MessageBarActions,
  MessageBarBody,
} from "@fluentui/react-components";
import { CreateUser } from "./CreateUser";
import { useSnapshot } from "valtio";
import { clearMessages, dismissMessage, messageStore } from "./messageStore";
import { DismissRegular } from "@fluentui/react-icons";

export function SettingsUsers() {
  const { settings } = useSettings();

  if (!settings) {
    return null;
  }

  return (
    <div className="flow centered">
      <div className="repel">
        <h1>Users</h1>
        <CreateUser />
      </div>

      <Banner />

      <TableWrapper>
        <UsersTable />
      </TableWrapper>
    </div>
  );
}

function Banner() {
  const snap = useSnapshot(messageStore);

  useEffect(() => {
    return clearMessages();
  }, []);

  if (!snap.messages.length) {
    return null;
  }

  return snap.messages.map((m) => (
    <MessageBar key={m.id} intent={m.intent}>
      <MessageBarBody>{m.message}</MessageBarBody>
      <MessageBarActions
        containerAction={
          <Button
            aria-label="dismiss"
            appearance="transparent"
            icon={<DismissRegular />}
            onClick={() => dismissMessage(m.id)}
          />
        }
      />
    </MessageBar>
  ));
}
