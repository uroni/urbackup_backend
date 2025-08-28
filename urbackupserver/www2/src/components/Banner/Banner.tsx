import {
  MessageBar,
  MessageBarBody,
  MessageBarActions,
  Button,
  MessageBarTitle,
} from "@fluentui/react-components";
import { DismissRegular } from "@fluentui/react-icons";
import { useEffect } from "react";
import { useSnapshot } from "valtio";

import {
  clearMessages,
  dismissMessage,
  Message,
  messageStore,
} from "./messageStore";

export function Banner() {
  const snap = useSnapshot(messageStore);

  useEffect(() => {
    return clearMessages();
  }, []);

  if (!snap.messages.length) {
    return null;
  }

  return snap.messages.map((message) => (
    <SingleBanner
      key={message.id}
      message={message}
      onClick={() => dismissMessage(message.id)}
    />
  ));
}

export function SingleBanner({
  message,
  onClick,
}: {
  message: Omit<Message, "id">;
  onClick?: () => void;
}) {
  return (
    <MessageBar
      intent={message.intent}
      role={message.intent === "error" ? "alert" : "status"}
    >
      <MessageBarBody>
        <MessageBarTitle>{message.title}</MessageBarTitle>
        {message.text}
      </MessageBarBody>
      <MessageBarActions
        containerAction={
          <Button
            aria-label="dismiss"
            appearance="transparent"
            icon={<DismissRegular />}
            onClick={onClick}
          />
        }
      />
    </MessageBar>
  );
}
