import { Button, Col, Progress, Row, Table } from "antd";
import produce from "immer";
import prettyBytes from "pretty-bytes";
import { useEffect, useRef, useState } from "react";
import { sleep, toIsoDateTime, useMountEffect } from "./App";
import { WizardComponent, WizardState } from "./WizardState";
import { Sparklines, SparklinesLine, SparklinesReferenceLine } from 'react-sparklines';

interface LogTableItem {
    time: Date,
    message: string
}

interface DownloadStats {
    totalBytes: number;
    doneBytes: number;
    speedBpms: number;
    pcdone: number;
}

interface RestoreStatus {
    action: string;
    eta_ms: number;
    percent_done: number;
    process_id: number;
    server_status_id: number;
    speed_bpms: number;
    total_bytes: number;
    done_bytes: number;
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
    const [downloadStats, setDownloadStats] = useState<DownloadStats|null>(null);
    const [speedHist, setSpeedHist] = useState<number[]>([])

    const logTableColumns = [
        {
          title: 'Time',
          dataIndex: 'time',
          key: 'time',
          width: 200,
          render: (t: Date) => toIsoDateTime(t),
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

    const restoreEcToString = (ec: number) => {
        switch(ec)
        {
            case 10:
                return "Error connecting to server";
            case 2:
                return "Error opening output device";
            case 3:
                return "Error reading image size from server";
            case 4:
                return "Timout while loading image (2)";
            case 5:
                return "Timout while loading image (1)";
            case 6:
                return "Writing image to disk failed";
            case 11:
                return "Disk too small for image";
            default:
                return "Unknown error (code " + ec + ")";
        }
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
                    if(jdata["ec"] === 0) {
                        addLog("Loading MBR finished");
                        setStatus("success");
                    } else {
                        addLog("Loading MBR failed: " + restoreEcToString(jdata["ec"]));
                        setStatus("exception");
                        return;
                    }
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

            setRestoreAction(props.props.restoreImage.letter +" - "+toIsoDateTime(new Date(props.props.restoreImage.time_s*1000)) + " client "+props.props.restoreImage.clientname);

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

            addLog("Restore is running")

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
                    const ec : number = jdata["ec"];
                    if(ec===0) {
                        addLog("Restoring image finished");
                        setPercent(100);
                        setStatus("success");
                        setImageDone(true);
                    } else {
                        addLog("Restoring image failed: "+restoreEcToString(ec));
                        setStatus("exception");
                        return;
                    }
                    break;
                } else if(typeof jdata["running_processes"]==="object") {
                    let restore_proc : RestoreStatus = jdata["running_processes"].find( 
                        (proc:RestoreStatus) => (proc.action && proc.action==="RESTORE_IMAGE")
                    );
                    if(restore_proc!==undefined) {
                        if(restore_proc.percent_done >=0)
                            setPercent(restore_proc.percent_done);

                        if(typeof restore_proc.done_bytes !== "undefined" )
                        {
                            setDownloadStats({
                                doneBytes: restore_proc.done_bytes,
                                totalBytes: restore_proc.total_bytes,
                                pcdone: restore_proc.percent_done,
                                speedBpms: restore_proc.speed_bpms
                            });
                            setSpeedHist(produce(
                                draft => {
                                    draft.push(restore_proc.speed_bpms*1000*8);
                                    if(draft.length>20)
                                    {
                                        draft.shift();
                                    }
                                }));
                        }
                    } else if(downloadStats!==null) {
                        setDownloadStats(produce( draft => {
                            draft.speed_bpms = 0;
                        }));
                    }
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
        Restoring {restoreAction}  progress: <br /> <br />
        <Progress percent={percent}  status={status} strokeWidth={30} strokeLinecap="square"/>
        { downloadStats!=null &&
            <><br /><br /><Row>
            <Col span={8}>
                <span style={{width: "50pt", display: "inline-block"}}>{prettyBytes(downloadStats.doneBytes)}</span> 
                    &nbsp; / {prettyBytes(downloadStats.totalBytes)} 
            </Col>
            <Col span={8}>
                <div style={{width: "300pt", display: "inline-block"}}>
                    <Sparklines data={speedHist} height={20}>
                        <SparklinesLine />
                        <SparklinesReferenceLine type="avg" />
                    </Sparklines>
                </div>
                <span style={{marginLeft: "10pt"}}>
                {prettyBytes(downloadStats.speedBpms*1000*8, {bits: true})} per second
                </span>
            </Col>
            </Row>
            </>
        }
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
        <div style={{overflow: "auto", height: "50vh", border: "1px solid black"}}>
            <Table pagination={false} showHeader={false} columns={logTableColumns} dataSource={logTableData} />
            <AlwaysScrollToBottom />
        </div>
    </div>);
}

export default Restoring;