import {
  useComboboxFilter,
  ComboboxProps,
  Combobox,
} from "@fluentui/react-components";
import { useId, useState } from "react";

import { ClientInfo } from "../api/urbackupserver";

export function SelectClientCombobox({
  clients,
  onSelect,
  defaultValue,
  label,
  placeholder,
  showLabel = true,
  hideAllClients,
}: {
  clients: ClientInfo[];
  onSelect: (value?: string) => void;
  defaultValue?: ClientInfo["name"];
  label?: string;
  placeholder?: string;
  showLabel?: boolean;
  hideAllClients?: boolean;
}) {
  const { onOptionSelect, query, setQuery, comboBoxChildren } =
    useClientCombobox(clients, onSelect, defaultValue, hideAllClients);

  const id = useId();
  const labelId = useId();

  return (
    <div className="cluster gutter-s">
      {showLabel && (
        <label htmlFor={id} id={labelId}>
          {label ?? "Select client"}
        </label>
      )}
      <Combobox
        id={id}
        aria-labelledby={labelId}
        placeholder={placeholder}
        onOptionSelect={onOptionSelect}
        onChange={(ev) => setQuery(ev.target.value)}
        onOpenChange={(_, data) => {
          if (data.open) {
            setQuery("");
          }
        }}
        value={query}
      >
        {comboBoxChildren}
      </Combobox>
    </div>
  );
}

function useClientCombobox(
  clients: ClientInfo[],
  onSelect: (value?: string) => void,
  defaultValue?: ClientInfo["name"],
  hideAllClients?: boolean,
) {
  const clientsOptions = clients.map((client) => ({
    children: client.name,
    value: String(client.id),
  }));

  const options = hideAllClients
    ? clientsOptions
    : [
        {
          children: "All clients",
          value: "all",
        },
        ...clientsOptions,
      ];

  const [query, setQuery] = useState<ClientInfo["name"]>(
    defaultValue ?? options[0].children,
  );

  const comboBoxChildren = useComboboxFilter(query, options, {
    optionToText: (d) => d.children,
    noOptionsMessage: `No results matched "${query}"`,
  });

  const onOptionSelect: ComboboxProps["onOptionSelect"] = (_, data) => {
    const text = data.optionValue;
    const selectedClient = clients.find((client) => String(client.id) === text);

    if (!selectedClient) {
      onSelect();
      setQuery(options[0].children);
      return;
    }

    onSelect(String(selectedClient.id));
    setQuery(selectedClient.name);
  };

  return {
    query,
    setQuery,
    onOptionSelect,
    comboBoxChildren,
  };
}
