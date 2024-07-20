import { Button, Combobox, ComboboxProps, makeStyles, OptionOnSelectData, Popover, PopoverSurface, PopoverTrigger, tokens, useComboboxFilter } from "@fluentui/react-components";
import { useId, useState } from "react";
import { urbackupServer } from "../../App";
import { OsType, StatusClientItem } from "../../api/urbackupserver";


const useStyles = makeStyles({
    surface: {
        marginInline: tokens.spacingHorizontalS,
    },
    combobox: {
        display: "grid",
        justifyItems: "start",
        gap: tokens.spacingHorizontalXS,
    },
    listbox: {
        translate: '-20px -40px'
    }
});



export function DownloadClient({
    clients,
    os,
    children
}: {
    clients: StatusClientItem[],
    os: OsType,
    children: React.ReactNode
}) {
    const styles = useStyles();
    const id = useId();

    const [open, setOpen] = useState(false);

    const options = clients.map(client => ({
        children: client.name, value: client.name
    }))

    const comboId = useId();

    const [query, setQuery] = useState<string>("");
    const comboBoxChildren = useComboboxFilter(query, options, {
        noOptionsMessage: `No results matched "${query}"`
    });

    const onOptionSelect: ComboboxProps["onOptionSelect"] = (_, data) => {
        const text = data.optionValue
        const selectedClient = clients.find(client => client.name === text)

        if (!selectedClient) {
            return
        }

        downloadClientFromId(selectedClient?.id, os)

        setOpen(false)
        setQuery("");
    };


    return (
        <Popover trapFocus positioning='above-start' open={open} onOpenChange={(_, data) => {
            const state = data.open ?? false
            setOpen(state)

            if (state === false) {
                setQuery("")
            }

        }}>
            <PopoverTrigger disableButtonEnhancement>
                <Button>{children}</Button>
            </PopoverTrigger>

            <PopoverSurface aria-labelledby={id}>
                <div className={styles.combobox}>
                    <label id={comboId}>Select client</label>
                    <Combobox
                        defaultOpen
                        positioning='before'
                        onOptionSelect={onOptionSelect}
                        aria-labelledby={comboId}
                        onChange={(ev) => setQuery(ev.target.value)}
                        value={query}
                        listbox={{
                            className: styles.listbox
                        }}
                    >
                        {comboBoxChildren}
                    </Combobox>
                </div>
            </PopoverSurface>
        </Popover >
    )
}

function downloadClientFromId(id: number, os: OsType) {
    location.href = urbackupServer.downloadClientURL(
        id,
        undefined,
        os
    );
}