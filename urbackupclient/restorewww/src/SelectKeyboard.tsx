import { Alert, Button, Select } from "antd";
import { useState } from "react";
import { useMountEffect } from "./App";
import { WizardComponent, WizardState } from "./WizardState";
import {getName} from "country-list";
import ReactCountryFlag from "react-country-flag"
import produce from "immer";


function SelectKeyboard(props: WizardComponent) {

    const [keyboardLayouts, setKeyboardLayouts] = useState<string[]>([]);
    const [isLoading, setIsLoading] = useState(true);
    const [fetchError, setFetchError] = useState("");
    const [selectedLayout, setSelectedLayout] = useState("us");

    useMountEffect( () => {
        (async () => {
            
            let jdata;
            try {
                const resp = await fetch("x?a=get_keyboard_layouts",
                    {method: "POST"})
                jdata = await resp.json();
            } catch(error) {
                setFetchError("Error while retrieving keyboard layouts");
                return;
            }

            if(!jdata || !jdata["layouts"]) {
                setFetchError("No layouts found");
                return;
            }

            const layouts: string[] = jdata["layouts"];

            setKeyboardLayouts(layouts);;
            setIsLoading(false);
        })();
    });

    const onSelectChange = (value: string) => {
        setSelectedLayout(value);
    }

    const selectKeyboardLayout = async () => {

        try {
            await fetch("x?a=set_keyboard_layout",
                {method: "POST",
                body: new URLSearchParams({
                    "layout": selectedLayout
                })})
        } catch(error) {
            setFetchError("Error setting keyboard layout");
            return;
        }

        props.update(produce(props.props, draft => {
            draft.keyboardLayout = selectedLayout;
            draft.state = WizardState.WaitForNetwork;
            draft.max_state = draft.state;
        }));
    }

    const skipNext = () => {
        props.update(produce(props.props, draft => {
            draft.state = WizardState.WaitForNetwork;
            draft.max_state = draft.state;
        }));
    }

    return (<div>
        Please select your keyboard layout: <br /> <br />
            <Select showSearch loading={isLoading} 
                onChange={onSelectChange} defaultValue={props.props.keyboardLayout} style={{width: "200pt"}}
                optionFilterProp="label">
                {keyboardLayouts.map( (layout:string) => {
                    return <Select.Option key={layout} value={layout} label={getName(layout) ? (getName(layout) + " "+layout) : layout}>
                        {getName(layout) &&
                        <>
                            <ReactCountryFlag countryCode={layout} />
                            &nbsp; {getName(layout)}
                        </>
                        } ({layout})
                    </Select.Option>;
                })
                }
            </Select>
        { fetchError.length>0 &&
            <Alert message={fetchError} type="error" />
        }
        <br />
        <br />
        <Button onClick={skipNext}>Skip</Button>
        <Button type="primary" htmlType="submit" onClick={selectKeyboardLayout} style={{marginLeft:"10pt"}}>
            Select keyboard layout
        </Button>
    </div>)
}

export default SelectKeyboard;