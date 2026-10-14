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
    const [isLoading, setIsLoading] = useState(true);

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
    let exitEffect = false;
    (async () => {
        if(props.props.internetServer) {
            console.log("Skip local server search because internet server is configured");

            let jdata;
            try {
                const resp = await fetch("x?a=configure_server",
                    {method: "POST",
                    body: new URLSearchParams({
                        "active": "1",
                        "url": props.props.serverUrl,
                        "authkey": props.props.serverAuthkey,
                        "proxy": props.props.serverProxy
                    }) });
                jdata = await resp.json();
            } catch(error) {
                setServiceError("Error retrieving data from HTTP server");
            }

            if(!jdata["ok"]) {
                setServiceError("Error configuring client to connect to server");
            } else {
                props.update(produce(props.props, draft => {
                    draft.state = WizardState.WaitForConnection;
                    draft.max_state = draft.state;
                }));
            }
            return;
        }

        console.log("Server search started");
        setIsLoading(false);

        var cnt = 0;
        while(!exitEffect) {
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

        if(exitEffect) {
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
    return () => {exitEffect = true};
    });
    
    return (<div>
        <Spin size="large" /> <br /> <br />
        Searching for UrBackup server in local network...
        <div style={{marginTop: "20pt"}}>
            <Button onClick={() => {
                props.update(produce(props.props, draft => {
                    draft.state = WizardState.ConfigureServerConnectionDetails;
                    draft.max_state = draft.state;
                }));
            } } loading={isLoading}>Configure Internet server</Button>
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