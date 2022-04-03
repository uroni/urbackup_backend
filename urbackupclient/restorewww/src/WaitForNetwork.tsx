import { Alert, Spin } from "antd";
import produce from "immer";
import { useState } from "react";
import { sleep, useMountEffect } from "./App";
import { WizardComponent, WizardState} from "./WizardState";


function WaitForNetwork(props: WizardComponent) {

    const [fetchError, setFetchError] = useState("");
    const [displayHint, setDisplayHint] = useState(false);
    
    useMountEffect(() => {
        let exitEffect = false;
        (async () => {
            var cnt = 0;
            while(!exitEffect) {
                let jdata;
                try {
                    const resp = await fetch("x?a=has_network_device",
                        {method: "POST"})
                    jdata = await resp.json();
                } catch(error) {
                    setFetchError("Error retrieving data from HTTP server");
                }

                if(exitEffect)
                    return;

                if(jdata && jdata["ret"]) {
                    props.update(produce(props.props, draft => {
                        draft.state = WizardState.SelectTimezone;
                        draft.max_state = draft.state;
                    }));
                    return;
                }

                await sleep(1000);

                ++cnt;
                if(cnt>30)
                    setDisplayHint(true);
            }
        })();
        return () => {exitEffect=true};
    });

    return (
        <div>
            <Spin size="large" /> <br /> <br />
            Waiting for network to become available...
            { displayHint &&
                <Alert message="Network does not seem to be configured automatically. Please configure network manually via the task tray" type="info" />
            }
            { fetchError.length>0 &&
                <Alert message={fetchError} type="error" />
            }
        </div>
    )
}

export default WaitForNetwork;
