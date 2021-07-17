import { Button, Progress, Table } from "antd";
import produce from "immer";
import { useEffect, useRef, useState } from "react";
import { sleep, useMountEffect } from "./App";
import { WizardComponent, WizardState } from "./WizardState";

interface LogTableItem {
    time: Date,
    message: string
}

const AlwaysScrollToBottom = () => {
    const elementRef = useRef<HTMLDivElement>(null);
    useEffect(() => {
        if(elementRef &&
            elementRef.current)
            elementRef.current.scrollIntoView()
    });
    return <div ref={elementRef} />;
  };

function Restoring(props: WizardComponent) {

    const [restoreAction, setRestoreAction] = useState("");
    const [status, setStatus] = useState<"normal" | "exception" | "success">("normal");
    const [percent, setPercent] = useState(0);
    const [imageDone, setImageDone] = useState(false);
    const [restartLoading, setRestartLoading] = useState(false);

    const logTableColumns = [
        {
          title: 'Time',
          dataIndex: 'time',
          key: 'time',
          width: 200,
          render: (t: Date) => t.toLocaleString(),
        },
        {
          title: 'Message',
          dataIndex: 'message',
          key: 'message',
        } ];

    const [logTableData, setLogTableData] = useState<LogTableItem[]>([]);

    const addLog = (msg: string) => {
        setLogTableData(produce(draft => {draft.push({time: new Date(), message: msg})}));
    }

    useMountEffect( () => {
        setRestoreAction("MBR and GPT");
        setPercent(0);

        addLog("Loading MBR and GPT data...");

        ( async () => {
            let jdata;
            try {
                const resp = await fetch("x?a=start_download",
                    {method: "POST",
                    body: new URLSearchParams({
                        "img_id": ("" + props.props.restoreImage.id),
                        "img_time": ("" + props.props.restoreImage.time_s),
                        "out": "/tmp/mbr.dat",
                        "mbr": "1"
                    })});
                jdata = await resp.json();
            } catch(error) {
                addLog("Error while loading MBR");
                setStatus("exception");
                return;
            }

            const mbr_res_id : number = jdata["res_id"];

            while(true) {
                await sleep(1000);

                try {
                    const resp = await fetch("x?a=download_progress",
                        {method: "POST",
                        body: new URLSearchParams({
                            "res_id": ("" + mbr_res_id)
                        })});
                    jdata = await resp.json();
                } catch(error) {
                    addLog("Error while checking MBR restore status");
                    setStatus("exception");
                    return;
                }

                if(jdata["finished"]) {
                    addLog("Loading MBR finished");
                    setStatus("success");
                    break;
                } else if(jdata["pc"]) {
                    setPercent(jdata["pc"]);
                }
            }

            addLog("Writing MBR and GPT to disk...");
            setPercent(0);
            setStatus("normal");

            try {
                const resp = await fetch("x?a=write_mbr",
                    {method: "POST",
                    body: new URLSearchParams({
                        "mbrfn": "/tmp/mbr.dat",
                        "out_device": props.props.restoreToDisk.path
                    })});
                jdata = await resp.json();
            } catch(error) {
                addLog("Error while writing MBR");
                setStatus("exception");
                return;
            }

            if(jdata["success"] && jdata["errmsg"] && jdata["errmsg"].length>0) {
                addLog("Ignoring MBR write error: "+jdata["errmsg"]);
            }

            if(!jdata["success"]) {
                addLog("Error writing to MBR and GPT: "+jdata["errmsg"]);
                setStatus("exception");
                return;
            }

            setRestoreAction(props.props.restoreImage.letter +" - "+(new Date(props.props.restoreImage.time_s)).toLocaleString() + " client "+props.props.restoreImage.clientname);

            addLog("Getting partition to restore to...")
            try {
                const resp = await fetch("x?a=get_partition",
                    {method: "POST",
                    body: new URLSearchParams({
                        "mbrfn": "/tmp/mbr.dat",
                        "out_device": props.props.restoreToDisk.path
                    })});
                jdata = await resp.json();
            } catch(error) {
                addLog("Error getting partition to restore to");
                setStatus("exception");
                return;
            }

            if(!jdata["success"]) {
                addLog("Error getting partition to restore to");
                setStatus("exception");
                return;
            }

            const partpath : string = jdata["partpath"];

            addLog("Restoring image to "+partpath);

            addLog("Starting image restore...");

            try {
                const resp = await fetch("x?a=start_download",
                    {method: "POST",
                    body: new URLSearchParams({
                        "img_id": ("" + props.props.restoreImage.id),
                        "img_time": ("" + props.props.restoreImage.time_s),
                        "out": partpath
                    })});
                jdata = await resp.json();
            } catch(error) {
                addLog("Error while starting image restore");
                setStatus("exception");
                return;
            }

            const img_res_id : number = jdata["res_id"];

            while(true) {
                await sleep(1000);

                try {
                    const resp = await fetch("x?a=download_progress",
                        {method: "POST",
                        body: new URLSearchParams({
                            "res_id": ("" + img_res_id)
                        })});
                    jdata = await resp.json();
                } catch(error) {
                    addLog("Error while checking image restore status");
                    setStatus("exception");
                    return;
                }

                if(jdata["finished"]) {
                    addLog("Restoring image finished");
                    setPercent(100);
                    setStatus("success");
                    setImageDone(true);
                    break;
                } else if(jdata["pc"]) {
                    setPercent(jdata["pc"]);
                }
            }
        })();
    })

    const restartSystem = async () => {
        setRestartLoading(true);
        addLog("Restarting machine...")
        try {
            await fetch("x?a=restart",
                {method: "POST"});
        } catch(error) {
            console.log(error);
            return;
        }
    }

    return (<div>
        Restoring {restoreAction}  progress: <br />
        <Progress percent={percent}  status={status}/>
        <br />
        <br />
        {status==="exception" &&
            <><Button type="primary" onClick={() => {
                props.update(produce(props.props, draft => {
                    draft.state = WizardState.ReviewRestore;
                    draft.max_state = draft.state;
                    draft.disableMenu = false;
                }))
            }}>Retry</Button>
            <br /><br /></>}
        {imageDone &&
            <>
                <Button onClick={() => {
                    props.update(produce(props.props, draft => {
                        draft.state = WizardState.ConfigRestore;
                        draft.max_state = draft.state;
                        draft.disableMenu = false;
                    }))
                }}>Restore another image</Button>
                <Button onClick={restartSystem}
                    loading={restartLoading}>Restart machine</Button>
            </>
        }
        <div style={{overflow: "auto", height: "500px", border: "1px solid black"}}>
            <Table pagination={false} showHeader={false} columns={logTableColumns} dataSource={logTableData} />
            <AlwaysScrollToBottom />
        </div>
    </div>);
}

export default Restoring;