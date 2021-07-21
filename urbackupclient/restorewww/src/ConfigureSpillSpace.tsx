import { Alert, Button, Spin } from "antd";
import Checkbox, { CheckboxChangeEvent } from "antd/lib/checkbox/Checkbox";
import produce from "immer";
import prettyBytes from "pretty-bytes";
import { useState } from "react";
import { useMountEffect } from "./App";
import { WizardComponent, WizardState } from "./WizardState";


interface ConfigSpillDisk {
    selected: boolean;
    model: string;
    maj_min: string;
    path: string;
    devpath: string;
    size: number;
    type: string;
    fstype: string;
    tested: boolean;
    space: number;
    destructive: boolean;
}

interface TestedDisk {
    path: string;
    space: number;
}


function ConfigureSpillSpace(props: WizardComponent) {

    const [remoteError, setRemoteError] = useState("");
    const [liveMedium, setLiveMedium] = useState(false);
    const [liveMediumSpace, setLiveMediumSpace] = useState(0);
    const [liveMediumSelected, setLiveMediumSelected] = useState(true);
    const [isLoading, setIsLoading] = useState(true);
    const [spillDisks, setSpillDisks] = useState<ConfigSpillDisk[]>([]);
    const [testing, setTesting] = useState(false);
    const [totalSpillSpace, setTotalSpillSpace] = useState(0);
    const [needsTest, setNeedsTest] = useState(false);

    useMountEffect( () => {
        ( async () => {

            let params = new URLSearchParams();

            if(props.props.restoreToDisk.path.length>0) {
                params.append("exclude", props.props.restoreToDisk.path);
            }

            let jdata;
            try {
                const resp = await fetch("x?a=get_spill_disks",
                    {method: "POST",
                     body: params});
                jdata = await resp.json();
            } catch(error) {
                setRemoteError("Error while getting clients on server");
                return;
            }
            if(typeof jdata["err"]!== "undefined") {
                setRemoteError(jdata["err"]);
                return;
            }

            if(jdata["live_medium"]) {
                setLiveMedium(true);
                setLiveMediumSpace(jdata["live_medium_space"]);
            }

            setSpillDisks(jdata["disks"].map(
                (disk: any) => { return {...disk, selected: false, tested: false, space: -1, destructive: false} }
            ));

            setIsLoading(false);
        })();
    });

    const testSpillDisks = async () => {
        setTesting(true);

        let p_disks = new URLSearchParams();

        let idx = 0;
        for(const spillDisk of spillDisks) {
            if(spillDisk.selected &&
                spillDisk.fstype!=="unknown" &&
                !spillDisk.tested) {
                p_disks.append("disk" + idx, spillDisk.path);
                idx+=1;
            }
        }

        let jdata : any;
        try {
            const resp = await fetch("x?a=test_spill_disks",
                {method: "POST",
                body: p_disks});
            jdata = await resp.json();
        } catch(error) {
            setRemoteError("Error while getting clients on server");
            return;
        }
        if(typeof jdata["err"]!== "undefined") {
            setRemoteError(jdata["err"]);
            return;
        }

        const testedDisks: TestedDisk[] = jdata["disks"];

        let addDiskSpace = 0;

        setSpillDisks(produce(draft => {
            for(let disk of draft) {
                if(disk.tested)
                    continue;

                if(disk.selected)
                    disk.tested=true;

                let found=false;
                for(const tdisk of testedDisks) {
                    if(tdisk.path===disk.path) {
                        disk.space = tdisk.space;
                        disk.destructive = false;
                        addDiskSpace+=tdisk.space;
                        found=true;
                    }
                }

                if(!found && disk.selected) {
                    disk.space = disk.size;
                    disk.destructive = true;
                    addDiskSpace += disk.size;
                }
            }
        }));

        setNeedsTest(false);
        setTesting(false);
        setTotalSpillSpace(totalSpillSpace + addDiskSpace);
    }    

    const useSpillSpace = () => {
        props.update(produce(props.props, draft => {
            draft.state = WizardState.ReviewRestore;
            draft.max_state = draft.state;

            draft.spillSpace.live_medium = liveMedium && liveMediumSelected;
            draft.spillSpace.live_medium_space = liveMediumSpace;

            draft.spillSpace.disks = [];

            for(const disk of spillDisks) {
                if(disk.selected && disk.tested) {
                    draft.spillSpace.disks.push({
                        space: disk.space,
                        fstype: disk.fstype,
                        model: disk.model,
                        path: disk.path,
                        size: disk.size,
                        destructive: disk.destructive
                    });
                }
            }
        }));
    }

    const spillDiskChange = (e: CheckboxChangeEvent, disk: ConfigSpillDisk) => {

        let disable_dev = "";
        let disable_dev_part = "";
        if(e.target.checked) {
            if(disk.devpath === disk.path)
                disable_dev_part = disk.devpath;
            else
                disable_dev = disk.devpath;
        }

        let addDiskSpace = 0;
        setSpillDisks(produce(draft => {
            draft.map((newdisk: ConfigSpillDisk) => {
                if(newdisk.path===disk.path) {
                    newdisk.selected=e.target.checked;
                    
                    if(newdisk.selected && newdisk.space>0)
                        addDiskSpace += newdisk.space;
                    else if(!newdisk.selected && newdisk.space>0)
                        addDiskSpace -= newdisk.space;

                    if(newdisk.selected && !newdisk.tested)
                        setNeedsTest(true);
                } else if( (newdisk.path===newdisk.devpath && newdisk.devpath===disable_dev) ||
                    (newdisk.path!==newdisk.devpath && newdisk.devpath===disable_dev_part)) {
                    if(newdisk.selected && newdisk.space>0)
                        addDiskSpace -= newdisk.space;
                    newdisk.selected=false;
                }
                return newdisk;
            })
        } ) );
        setTotalSpillSpace(totalSpillSpace + addDiskSpace);
    }

    return (<div>
            Select spill disks: <br /><br />

            {isLoading &&
                <><Spin />Loading...</>}

            {liveMedium &&
                <div><Checkbox checked={liveMediumSelected} onChange={ e => setLiveMediumSelected(e.target.checked)}>Live medium ({prettyBytes(liveMediumSpace)}</Checkbox></div> }
            
            {spillDisks.map( disk => {
                return <div><Checkbox checked={disk.selected} onChange={ e => spillDiskChange(e, disk) }>
                    {disk.type==="disk" ? "Disk" : "Partition"} at {disk.path} size {prettyBytes(disk.size)} file system {disk.fstype}
                    {disk.selected && disk.tested && disk.destructive &&
                        <Alert type="warning" message="Content of this disk will be overwritten" />
                    }
                </Checkbox> </div>
            })
            }

            <br /><br />

            {needsTest &&
                <Button loading={testing || isLoading} onClick={testSpillDisks}>Test spill disks</Button> }
            {!needsTest &&
                <Button loading={testing || isLoading} onClick={useSpillSpace}>
                    { ( (liveMedium && liveMediumSelected) || spillDisks.filter(disk => disk.selected).length>0 )
                        ? "Use spill space" : "Skip selecting spill space"}
                </Button> }

            {totalSpillSpace>0 &&
                <>
                    <br /> <br />
                    Total spill space: {prettyBytes(totalSpillSpace)}
                </>}

            { remoteError.length>0 &&
                <Alert message={remoteError} type="error" />
            }
    </div>);
}

export default ConfigureSpillSpace;