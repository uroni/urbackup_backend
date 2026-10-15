import { Button, Col, Progress, Row, Table } from "antd";
import produce from "immer";
import prettyBytes from "pretty-bytes";
import { useEffect, useRef, useState } from "react";
import { sleep, toIsoDateTime, useMountEffect } from "./App";
import { BackupImage, LocalDisk, WizardComponent, WizardState } from "./WizardState";
import { Sparklines, SparklinesLine, SparklinesReferenceLine } from 'react-sparklines';
import assert from "assert";
import { imageDateTime } from "./ConfigRestore";

interface LogTableItem {
    time: Date;
    message: string;
    key: number;
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

interface MbrInfo {
    fn: string;
    wholeDisk: boolean;
}

type ScrollLogT = {
    logItems: LogTableItem[];
};

function AlwaysScrollToBottom(logItems: ScrollLogT) {
    const elementRef = useRef<HTMLDivElement>(null);
    useEffect(() => {
        if(elementRef &&
            elementRef.current)
            elementRef.current.scrollIntoView()
    }, [logItems.logItems]);
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
    const [retryWithSpillSpace, setRetryWithSpillSpace] = useState(false);
    const [diskSizeError, setDiskSizeError] = useState(false);
    const [triedWithSpillSpace, setTriedWithSpillSpace] = useState(false);

    const logId = useRef(0);

    //Needed for dev, otherwise it would restart restore on hot-reload
    let restoreRunning = useRef(false);

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
        const currLogId = logId.current++;
        setLogTableData(produce(draft => {draft.push({time: new Date(), message: msg, key: currLogId})}));
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

    interface GetMbrRes 
    {
        ok: boolean;
        tmpfn: string;
    }

    const isDiskMBR = async (mbrfn: string) : Promise<boolean> => {
        try {
            const resp = await fetch("x?a=get_is_disk_mbr", {
                method: "POST",
                body: new URLSearchParams({
                    "mbrfn": mbrfn
                })
            });
            const jdata = await resp.json();
            if(!jdata["ok"])
                throw Error("get_is_disk_mbr response not ok");

            return jdata["res"];
        } catch(error) {
            addLog("Error while getting if this is a disk MBR");
            setStatus("exception");
            return false;
        }
    }

    const getMBR = async (restoreImage: BackupImage) : Promise<GetMbrRes> => {

        addLog("Loading MBR and GPT data...");

        let jdata;

        try {
            const resp = await fetch("x?a=get_tmpfn",
                {method: "POST"});
            jdata = await resp.json();
        } catch(error) {
            addLog("Error while getting temporary file name");
            setStatus("exception");
            return {ok: false, tmpfn: ""};
        }
        
        if(typeof jdata["err"]!=="undefined") {
            addLog("Error while getting temporary file name: "+jdata["err"]);
            setStatus("exception");
            return {ok: false, tmpfn: ""};
        }

        const tmpfn : string = jdata["fn"];
        
        try {
            const resp = await fetch("x?a=start_download",
                {method: "POST",
                body: new URLSearchParams({
                    "img_id": ("" + restoreImage.id),
                    "img_time": ("" + restoreImage.time_s),
                    "out": tmpfn,
                    "mbr": "1"
                })});
            jdata = await resp.json();
        } catch(error) {
            addLog("Error while loading MBR");
            setStatus("exception");
            return {ok: false, tmpfn: ""};
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
                return {ok: false, tmpfn: ""};
            }

            if(jdata["finished"]) {
                if(jdata["ec"] === 0) {
                    addLog("Loading MBR finished");
                    setStatus("success");
                } else {
                    addLog("Loading MBR failed: " + restoreEcToString(jdata["ec"]));
                    setStatus("exception");
                    return {ok: false, tmpfn: ""};
                }
                break;
            } else if(jdata["pc"]) {
                setPercent(jdata["pc"]);
            }
        }

        return {ok: true, tmpfn: tmpfn};
    }

    const restoreMBR = async () : Promise<MbrInfo>  => {
        setRestoreAction("MBR and GPT");
        setPercent(0);

        const errRet = {fn: "", wholeDisk: false};
        const gmbr_res = await getMBR(props.props.restoreImage);
        if(!gmbr_res.ok)
            return errRet;

        if(await isDiskMBR(gmbr_res.tmpfn)) {
            addLog("MBR has disk information. Restoring whole disk...");
            setStatus("normal");
            return {fn: gmbr_res.tmpfn, wholeDisk: true};
        }

        addLog("Writing MBR and GPT to disk...");
        setPercent(0);
        setStatus("normal");

        let jdata;
        try {
            const resp = await fetch("x?a=write_mbr",
                {method: "POST",
                body: new URLSearchParams({
                    "mbrfn": gmbr_res.tmpfn,
                    "out_device": props.props.restoreToDisk.path
                })});
            jdata = await resp.json();
        } catch(error) {
            addLog("Error while writing MBR");
            setStatus("exception");
            return errRet;
        }

        if(jdata["success"] && jdata["errmsg"] && jdata["errmsg"].length>0) {
            addLog("Ignoring MBR write error: "+jdata["errmsg"]);
        }

        if(!jdata["success"]) {
            addLog("Error writing to MBR and GPT: "+jdata["errmsg"]);
            setStatus("exception");
            return errRet;
        }

        return {fn: gmbr_res.tmpfn, wholeDisk: false};
    }

    const resizeSpilledImage = async (disk_fn: string, new_size: string, partnum: number, orig_dev_fn: string) => {
        addLog("Resizing partition...");

        let jdata;
        try {
            const resp = await fetch("x?a=resize_disk",
                {method: "POST",
                body: new URLSearchParams({
                    "disk_fn": disk_fn,
                    "new_size": new_size
                })});
            jdata = await resp.json();
        } catch(error) {
            addLog("Error starting partition resize");
            setStatus("exception");
            return false;
        }

        const res_id : number = jdata["res_id"];

        while(true) {
            await sleep(1000);

            try {
                const resp = await fetch("x?a=restore_finished",
                    {method: "POST",
                    body: new URLSearchParams({
                        "res_id": ("" + res_id)
                    })});
                jdata = await resp.json();
            } catch(error) {
                addLog("Error while checking image restore status");
                setStatus("exception");
                return false;
            }

            if(jdata["finished"]) {
                setPercent(100);
                if(typeof jdata["err"]!=="undefined") {
                    addLog("Error resizing partition: "+jdata["err"]);
                    setStatus("exception");
                    return false; 
                }

                addLog("Resizing done. Cleaning up spill disk...");

                try {
                    await fetch("x?a=cleanup_spill_disks",
                        {method: "POST"});
                } catch(error) {
                    addLog("Error cleaning up spill disk");
                }

                if(!props.props.restoreToPartition) {
                    addLog("Resizing partition...");

                    let jdata;
                    try {
                        const resp = await fetch("x?a=resize_part",
                            {method: "POST",
                            body: new URLSearchParams({
                                "disk_fn": orig_dev_fn,
                                "new_size": new_size,
                                "partnum": (""+partnum)
                            })});
                        jdata = await resp.json();
                    } catch(error) {
                        addLog("Error resizing partition");
                    }

                    if(jdata["err"]) {
                        addLog("Error changing partition size. Perhaps fix with gparted? Output: "+ jdata["err"]);
                    }
                }

                addLog("Done.");

                return true;
            } else if(typeof jdata["pcdone"]==="number") {
                setPercent(jdata["pcdone"]);
            }
        }
    }

    const restoreImage = async (img: BackupImage, restoreToPartition: boolean,
        restoreToDisk: LocalDisk, withSpillSpace: boolean, mbrInfo: MbrInfo) => {
        setRestoreAction(img.letter +" - "+imageDateTime(img, props.props) + " client "+img.clientname);

        let partpath: string;
        let partnum: number;
        if(!restoreToPartition) {

            if(mbrInfo.fn.length===0) {
                addLog("MBR info not found. Cannot restore to partition")
                return false;
            }
            
            if(!mbrInfo.wholeDisk) {            
                const currMbr = await getMBR(img);
                if(!currMbr.ok) {
                    addLog("Error loading MBR of partition.")
                    return false;
                }

                setStatus("normal");

                addLog("Getting partition to restore to...");
                let jdata;
                try {
                    const resp = await fetch("x?a=get_partition",
                        {method: "POST",
                        body: new URLSearchParams({
                            "mbrfn": currMbr.tmpfn,
                            "out_device": restoreToDisk.path
                        })});
                    jdata = await resp.json();
                } catch(error) {
                    addLog("Error getting partition to restore to");
                    setStatus("exception");
                    return false;
                }

                if(!jdata["success"]) {
                    addLog("Error getting partition to restore to");
                    setStatus("exception");
                    return false;
                }

                partpath = jdata["partpath"];
                partnum = jdata["partnum"];
            } else {
                partpath = restoreToDisk.path;
                partnum = -1;
            }
        } else {
            partpath = restoreToDisk.path;
            partnum = -1;
        }

        addLog("Restoring image to "+partpath);

        let orig_dev_sz = "";

        if(withSpillSpace)
        {
            addLog("Setting up spill space...");

            let params = new URLSearchParams({
                "orig_dev": partpath
            });

            let idx=0;
            if(props.props.spillSpace.live_medium) {
                params.append("disk"+idx, "live_medium");
                params.append("destructive"+idx, "0");
                ++idx;
            }

            for(const sp of props.props.spillSpace.disks) {
                params.append("disk"+idx, sp.path);
                params.append("destructive"+idx, sp.destructive ? "1" : "0");
            }

            let jdata;
            try {
                const resp = await fetch("x?a=setup_spill_disks",
                    {method: "POST",
                    body: params});
                jdata = await resp.json();
            } catch(error) {
                addLog("Error while setting up spill space");
                setStatus("exception");
                return false;
            }

            if(typeof jdata["err"]!=="undefined") {
                addLog("Error while setting up spill space: "+jdata["err"]);
                setStatus("exception");
                return false;
            }

            partpath = jdata["path"];
            orig_dev_sz = jdata["orig_size"];
        }

        addLog("Starting image restore...");

        let jdata;
        try {
            const resp = await fetch("x?a=start_download",
                {method: "POST",
                body: new URLSearchParams({
                    "img_id": ("" + img.id),
                    "img_time": ("" + img.time_s),
                    "out": partpath
                })});
            jdata = await resp.json();
        } catch(error) {
            addLog("Error while starting image restore");
            setStatus("exception");
            return false;
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
                return false;
            }

            if(jdata["finished"]) {
                const ec : number = jdata["ec"];

                if(ec===11 ) {
                    setDiskSizeError(true);

                    if(withSpillSpace)
                    {
                        setTriedWithSpillSpace(true);
                    }
                }

                if(ec===11 && 
                    (props.props.spillSpace.live_medium ||
                        props.props.spillSpace.disks.length>0)) {
                    setRetryWithSpillSpace(true);
                }

                if(ec===0) {
                    addLog("Restoring image finished");
                    setPercent(100);
                    setStatus("success");

                    if(withSpillSpace)
                    {
                        setStatus("normal");
                        if(!await resizeSpilledImage(partpath, orig_dev_sz, partnum, restoreToDisk.path))
                        {
                            setStatus("exception");
                            return false;
                        }
                    }
                } else {
                    addLog("Restoring image failed: "+restoreEcToString(ec));
                    setStatus("exception");
                    return false;
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
                        if(draft)
                            draft.speedBpms = 0;
                    }));
                }
            }
        }

        return true;
    };

    const runRestore = async (withSpillSpace: boolean) => {
        if(restoreRunning.current)
            return;

        restoreRunning.current = true;

        setStatus("normal");
        let mbrInfo = {fn: "", wholeDisk: false};
        if(!props.props.restoreToPartition) {
            mbrInfo = await restoreMBR();

            if(props.props.restoreOnlyMBR)
                return;
        }


        let restoreImages = [props.props.restoreImage];

        for(const assoc_img of props.props.restoreImage.assoc)
            restoreImages.push(assoc_img);

        if(restoreImages.length>1)
            addLog("Restoring "+restoreImages.length+" images of client "+props.props.restoreImage.clientname+": ");

        for(const img of restoreImages) {
            addLog("Restoring "+img.letter+" at " + imageDateTime(img, props.props));
        }

        assert(!props.props.restoreToPartition || restoreImages.length===1);

        let restore_ok: boolean = true;
        for(const img of restoreImages) {
            if(!await restoreImage(img, props.props.restoreToPartition,
                props.props.restoreToDisk, withSpillSpace, mbrInfo) ) {
                restore_ok = false;
                break;
            }
            withSpillSpace = false;
        }

        if(restore_ok)
            setImageDone(true);

        restoreRunning.current = false;
    }

    useMountEffect( () => {
        (async () => {
            await runRestore(false);            
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

    const doRetryWithSpillSpace = async () => {
        setDiskSizeError(false);
        await runRestore(true);
    }

    const configureSpillSpace = async () => {
        props.update(produce(props.props, draft => {
            draft.state = WizardState.ConfigSpillSpace;
            draft.disableMenu = false;
        }));
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
            <>
            {(!diskSizeError || triedWithSpillSpace ) &&
                <><Button type="primary" onClick={() => {
                props.update(produce(props.props, draft => {
                    draft.state = WizardState.ReviewRestore;
                    draft.max_state = draft.state;
                    draft.disableMenu = false;
                }))
            }}>Retry</Button></>}
            {!retryWithSpillSpace && diskSizeError && props.props.canRestoreSpill &&
                    <><br /><br /><Button type="primary" onClick={configureSpillSpace}>
                        Configure spill space to restore to smaller disk
                    </Button></>
                }
                {retryWithSpillSpace && props.props.canRestoreSpill &&
                    <><br /><br /><Button type="primary" onClick={doRetryWithSpillSpace}>
                    Retry with configured spill space
                </Button></>
                }
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
                    loading={restartLoading} style={{marginLeft: "10pt"}}>Restart machine</Button>
                <br /> <br />
            </>
        }
        <div style={{overflow: "auto", height: "50vh", border: "1px solid black"}}>
            <Table pagination={false} showHeader={false} columns={logTableColumns} dataSource={logTableData} />
            <AlwaysScrollToBottom logItems={logTableData}/>
        </div>
    </div>);
}

export default Restoring;