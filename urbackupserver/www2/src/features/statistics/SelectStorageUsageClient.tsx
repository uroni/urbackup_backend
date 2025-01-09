import {
  useComboboxFilter,
  ComboboxProps,
  Combobox,
} from "@fluentui/react-components";
import { useId, useState } from "react";
import { ClientInfo } from "../../api/urbackupserver";

export function SelectStorageUsageClient({
  clients,
  onSelect,
}: {
  clients: ClientInfo[];
  onSelect: (value?: string) => void;
}) {
  const options = [
    {
      children: "All clients",
      value: "all",
    },
    ...clients.map((client) => ({
      children: client.name,
      value: String(client.id),
    })),
  ];

  const comboId = useId();

  const [query, setQuery] = useState<string>(options[0].children);
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

  return (
    <div className="cluster">
      <label id={comboId}>Select client</label>
      <Combobox
        aria-labelledby={comboId}
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
