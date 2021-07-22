import { Alert, Button, Modal, Spin } from "antd";
import produce from "immer";
import { useState } from "react";
import { sleep, useMountEffect } from "./App";
import { WizardComponent, WizardState } from "./WizardState";

function WaitForConnection(props: WizardComponent) {
    const [connectionState, setConnectionState] = useState("");
    const [serviceError, setServiceError] = useState("");

    enum ConnectedResult {
        ServiceError,
        Connected,
        NotConnected
    };

    interface SConnectedResult {
        res: ConnectedResult,
        err: string
    }

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
            return {res: ConnectedResult.ServiceError,
                err: "Unknown status returned"};
        }

        if (jdata["servers"].length>0) {
            return {res: ConnectedResult.Connected,
                err: ""};    
        }

        if(typeof jdata["internet_status"] !== "undefined")
        {
            return {res: ConnectedResult.NotConnected,
                err: jdata["internet_status"] };
        }
        
        return {res: ConnectedResult.NotConnected,
            err: ""};
    }

    useMountEffect(() => {
    let exitEffect = false;
    (async () => {
        console.log("Server wait for connection started");

        var cnt = 0;
        while(!exitEffect) {
            const res = await checkConnected();
            if(res.res===ConnectedResult.Connected)
                break;

            if(res.err.indexOf("initializing")===-1 &&
                res.err.indexOf("wait_local")===-1 &&
                res.err.indexOf("connected") === -1) {
                setServiceError(res.err);
                setConnectionState("");
            } else {
                setConnectionState(res.err);
                setServiceError("");
            }

            cnt+=1;
            if(cnt>60 || 
                res.err==="connecting_failed" ||
                res.err.indexOf("error:")===0 ) {                
                Modal.error({
                    title: "Error connecting to service",
                    content: "Connecting to server failed: "+res.err,
                    onOk: () => {
                        props.update(produce(props.props, draft => {
                            draft.state = WizardState.ConfigureServerConnectionDetails;
                        }));
                    }
                });
                return;
            }

            await sleep(1000);
        }

        if(exitEffect) {
            console.log("Server wait for connection aborted");
            return;
        }

        console.log("Server connected");
        props.update(produce(props.props, draft => {
            draft.serverFound=true;
            draft.state = WizardState.LoginToServer;
            draft.max_state = draft.state;
        }));
    })() ;
    return () => { exitEffect=true};
    });

    return (<div>
        <Spin size="large" /> <br /> <br />
        Waiting for connection to server to be established...
        {connectionState.length>0 &&
            <div style={{marginTop: "10px"}}>
                <Alert message={connectionState} type="info" />
            </div>
        }
        {serviceError.length>0 &&
            <div style={{marginTop: "10px"}}>
                <Alert message={serviceError} type="error" />
                <div style={{marginTop: "10px"}}>
                    <Button onClick={()=> {
                        props.update(produce(props.props, draft => {
                        draft.state = WizardState.ConfigureServerConnectionDetails;
                    }))} }>Change connection configuration</Button>
                </div>
            </div>
        }
    </div>);
}

export default WaitForConnection;