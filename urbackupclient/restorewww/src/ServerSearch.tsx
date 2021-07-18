import { Alert, Button, Spin } from "antd";
import produce from "immer";
import { useState } from "react";
import { sleep, useMountEffect } from "./App";
import { WizardComponent, WizardState } from "./WizardState";

function ServerSearch(props: WizardComponent) {

    enum ConnectedResult
    {
        ServiceError,
        Connected,
        NotConnected
    };

    interface SConnectedResult {
        res: ConnectedResult,
        err: string
    }

    const [serviceError, setServiceError] = useState("");

    const [noLocalServer, setNoLocalServer] = useState(false);

    const checkConnected = async () : Promise<SConnectedResult> => {
        try {
            const resp = await fetch("x?a=status",
                {method: "POST"})
            var jdata = await resp.json();
        } catch(error) {
            return {res: ConnectedResult.ServiceError,
                    err: "Error retrieving data from HTTP server"};
        }

        if (typeof jdata["servers"] ==="undefined") {
            
            if(jdata["err"]) {
                return {res: ConnectedResult.ServiceError,
                    err: jdata["err"]};    
            }

            return {res: ConnectedResult.ServiceError,
                err: "Unknown status returned"};
        }

        if (jdata["servers"].length>0) {
            return {res: ConnectedResult.Connected,
                err: ""};    
        }
        
        return {res: ConnectedResult.NotConnected,
            err: ""};
    }

    useMountEffect(() => {
    (async () => {
        console.log("Server search started");

        var cnt = 0;
        while(props.props.state===WizardState.ServerSearch) {
            const res = await checkConnected();
            if(res.res===ConnectedResult.Connected)
                break;

            setServiceError(res.err);

            cnt+=1;
            if(cnt>60) {
                //Local server should be found by now
                setNoLocalServer(true);
            }

            await sleep(1000);
        }

        if(props.props.state!==WizardState.ServerSearch) {
            console.log("Server search aborted");
            return;
        }

        console.log("Server found");
        props.update(produce(props.props, draft => {
            draft.serverFound=true;
            draft.state = WizardState.LoginToServer;
            draft.max_state = draft.state;
        }));
    })() ;
    });
    
    return (<div>
        <Spin size="large" /> <br /> <br />
        Searching for UrBackup server in local network...
        <div>
            <Button onClick={() => {
                props.update(produce(props.props, draft => {
                    draft.state = WizardState.ConfigureServerConnectionDetails;
                    draft.max_state = draft.state;
                }));
            } }>Configure Internet server</Button>
        </div>
        {noLocalServer &&
            <><br /><Alert
            message="No local server found. Please configure an Internet server or make sure the server runs"
            type="info"
          /></>
        }
        {serviceError.length>0 &&
            <><br /><Alert message={serviceError} type="error" /></>
        }
    </div>);
}

export default ServerSearch;