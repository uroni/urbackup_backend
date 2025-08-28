import { MessageBarIntent } from "@fluentui/react-components";
import { proxy } from "valtio";

export interface Message {
  id: string;
  text: string;
  intent?: MessageBarIntent;
  title?: string;
}

export const messageStore = proxy<{ messages: Message[] }>({
  messages: [],
});

export function addMessage(message: Omit<Message, "id">) {
  messageStore.messages.unshift({
    ...message,
    id: crypto.randomUUID(),
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
