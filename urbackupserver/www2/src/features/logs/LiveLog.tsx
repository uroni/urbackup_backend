import {
  Button,
  Combobox,
  ComboboxProps,
  makeStyles,
  Popover,
  PopoverSurface,
  PopoverTrigger,
  tokens,
  useComboboxFilter,
} from "@fluentui/react-components";
import { useId, useState } from "react";
import { Open16Regular } from "@fluentui/react-icons";

import type { LogClient } from "../../api/urbackupserver";

const useStyles = makeStyles({
  combobox: {
    display: "grid",
    justifyItems: "start",
    gap: tokens.spacingVerticalXS,
  },
  listbox: {
    translate: `0 ${tokens.spacingVerticalS}`,
  },
});

export function LiveLog({
  clients,
  children,
}: {
  clients: LogClient[];
  children: React.ReactNode;
}) {
  const styles = useStyles();
  const id = useId();

  const [open, setOpen] = useState(false);

  const options = clients.map((client) => ({
    children: client.name,
    value: client.name,
  }));

  const comboId = useId();

  const [query, setQuery] = useState<string>("");
  const comboBoxChildren = useComboboxFilter(query, options, {
    noOptionsMessage: `No results matched "${query}"`,
  });

  const onOptionSelect: ComboboxProps["onOptionSelect"] = (_, data) => {
    const text = data.optionValue;
    const selectedClient = clients.find((client) => client.name === text);

    if (!selectedClient) {
      return;
    }

    window.open("", "_blank")?.focus();

    setOpen(false);
    setQuery("");
  };

  return (
    <Popover
      trapFocus
      open={open}
      onOpenChange={(_, data) => {
        const state = data.open ?? false;
        setOpen(state);

        if (state === false) {
          setQuery("");
        }
      }}
    >
      <PopoverTrigger disableButtonEnhancement>
        <Button icon={<Open16Regular />}>{children}</Button>
      </PopoverTrigger>

      <PopoverSurface aria-labelledby={id}>
        <div className={styles.combobox}>
          <label id={comboId}>Select client</label>
          <Combobox
            defaultOpen
            onOptionSelect={onOptionSelect}
            aria-labelledby={comboId}
            onChange={(ev) => setQuery(ev.target.value)}
            value={query}
            listbox={{
              className: styles.listbox,
            }}
          >
            {comboBoxChildren}
          </Combobox>
        </div>
      </PopoverSurface>
    </Popover>
  );
}
