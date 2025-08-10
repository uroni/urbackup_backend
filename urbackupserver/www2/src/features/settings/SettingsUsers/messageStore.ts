import { MessageBarIntent } from "@fluentui/react-components";
import { proxy } from "valtio";

interface Message {
  id: string;
  intent: MessageBarIntent;
  message: string;
}

export const messageStore = proxy<{ messages: Message[] }>({
  messages: [],
});

export function addMessage(
  intent: Message["intent"],
  message: Message["message"],
) {
  messageStore.messages.push({
    id: crypto.randomUUID(),
    intent,
    message,
  });
}

export function dismissMessage(id: Message["id"]) {
  const index = messageStore.messages.findIndex((m) => m.id === id);
  if (index >= 0) {
    messageStore.messages.splice(index, 1);
  }
}

export function clearMessages() {
  messageStore.messages = [];
}
