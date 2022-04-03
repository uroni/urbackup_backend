import { Alert, Button, Radio, RadioChangeEvent, Select, Space } from "antd";
import Checkbox, { CheckboxChangeEvent } from "antd/lib/checkbox/Checkbox";
import produce from "immer";
import { useCallback, useEffect } from "react";
import { useState } from "react";
import { serverTimezoneDef, toIsoDateTime } from "./App";
import { BackupImage, LocalDisk, WizardComponent, WizardState, WizardStateProps } from "./WizardState";

export function imageDateTime(img: BackupImage, props: WizardStateProps) {
    if (img.time_str.length>0 && props.timezoneArea===serverTimezoneDef)
        return img.time_str;
        
    return toIsoDateTime(new Date(img.time_s*1000))
}

function ConfigRestore(props: WizardComponent) {
    const [remoteError, setRemoteError] = useState("");
    const [hasClients, setHasClients] = useState(false);
    const [hasImages, setHasImages] = useState(true);
    const [clients, setClients] = useState<string[]>([]);
    const [images, setImages] = useState<BackupImage[]>([]);
    const [flattenedImages, setFlattenedImages] = useState<BackupImage[]>([]);
    const [selImage, setSelImage] = useState< BackupImage | undefined>(undefined);
    const [hasDisks, setHasDisks] = useState(false);
    const [disks, setDisks] = useState<LocalDisk[]>([]);
    const [selDisk, setSelDisk] = useState<LocalDisk | undefined>(undefined);
    const [advanced, setAdvanced] = useState(false);

    const init = useCallback(async () => {

        if(!hasClients) {
            let jdata;
            try {
                const resp = await fetch("x?a=get_clientnames",
                    {method: "POST"});
                jdata = await resp.json();
            } catch(error) {
                setRemoteError("Error while getting clients on server");
                return;
            }
            if(typeof jdata["err"]!== "undefined") {
                setRemoteError(jdata["err"]);
                return;
            }
            setClients(jdata["clients"]);
            setHasClients(true);
        }

        if(!hasDisks) {
            let jdata;
            if(!props.props.restoreToPartition) {
                try {
                    const resp = await fetch("x?a=get_disks",
                        {method: "POST"});
                    jdata = await resp.json();
                } catch(error) {
                    setRemoteError("Error while getting local disks");
                    return;
                }
            } else {
                try {
                    const resp = await fetch("x?a=get_disks",
                        {method: "POST",
                        body: new URLSearchParams({
                            "partitions": "1"
                        })});
                    jdata = await resp.json();
                } catch(error) {
                    setRemoteError("Error while getting local disks");
                    return;
                }
            }

            setDisks(jdata["disks"]);
            setHasDisks(true);
        }
    }, [hasClients, hasDisks, props.props.restoreToPartition]);

    useEffect( () => {
        init();
    }, [init]);

    const onClientChange = async (value: string) => {
        setHasImages(false);
        let jdata;
        try {
            const resp = await fetch("x?a=get_backupimages",
                {method: "POST",
                body: new URLSearchParams({
                    "restore_name": value
                }) });
            jdata = await resp.json();
        } catch(error) {
            setRemoteError("Error while getting images of client on server");
            return;
        }

        if(typeof jdata["err"]!== "undefined") {
            setRemoteError(jdata["err"]);
            return;
        }

        let new_images : BackupImage[] = jdata["images"].map(
            (image: any) => ({...image, "clientname": value}));

        for(let img of new_images) {
            for(let assoc_img of img.assoc) {
                assoc_img.clientname = img.clientname;
                if(assoc_img.letter.length===0)
                    assoc_img.letter = "sysvol";
            }
        }
        
        setImages(new_images);

        let new_images_flattened = [];

        for(const img of new_images) {
            let img_no_assoc : BackupImage = {...img};
            img_no_assoc.assoc = [];
            new_images_flattened.push(img_no_assoc);
            for(const assoc_img of img.assoc) {
                new_images_flattened.push(assoc_img);
            }
        }

        setFlattenedImages(new_images_flattened);

        setHasImages(true);
    }

    const advanceWizard = () => {
        if (selImage===undefined) {
            setRemoteError("Please select an image to restore");
            return;
        }

        if(selDisk===undefined) {
            setRemoteError("Please select a disk to restore to");
            return;
        }

        props.update(produce(props.props, draft => {
            draft.restoreImage = selImage;
            draft.restoreToDisk = selDisk;
            draft.state = WizardState.ReviewRestore;
            draft.max_state = draft.state;
        }));
    }

    const advancedChange = (e : CheckboxChangeEvent) => {
        setAdvanced(e.target.checked);
    }

    const onRestoreChange = async (e: RadioChangeEvent) => {
        if(e.target.value==="normal") {
            if(selDisk && selDisk.type==="part") {
                setSelDisk(undefined);
            }

            props.update(produce(props.props, draft => {
                draft.restoreOnlyMBR = false;
                draft.restoreToPartition = false;
            }));

            setHasDisks(false);
        } else if(e.target.value==="restoreMBR") {
            if(selDisk && selDisk.type==="part") {
                setSelDisk(undefined);
            }

            props.update(produce(props.props, draft => {
                draft.restoreOnlyMBR = true;
                draft.restoreToPartition = false;
            }));

            setHasDisks(false);
        } else if(e.target.value==="restorePartition") {

            if(selDisk && selDisk.type!=="part") {
                setSelDisk(undefined);
            }

            props.update(produce(props.props, draft => {
                draft.restoreOnlyMBR = false;
                draft.restoreToPartition = true;
            }));

            setHasDisks(false);
        }
    }

    useEffect( () => {
        if (selImage===undefined)
            return;

        const getSelImageSet = () => props.props.restoreToPartition ? flattenedImages : images;
        setSelImage(getSelImageSet().find( image => image.id === selImage.id));
    }, [selImage, flattenedImages, images, props.props.restoreToPartition]); 

    const getSelImageSet = () => props.props.restoreToPartition ? flattenedImages : images;

    const renderAdvanced = () => {
        return (<><br /> <br />
        <Radio.Group onChange={onRestoreChange} value={props.props.restoreOnlyMBR ? "restoreMBR" :
                    (props.props.restoreToPartition ? "restorePartition" : "normal")}>
            <Space direction="vertical">
                <Radio value="normal">Restore MBR and GPT, then restore volume to correct partition</Radio>
                <Radio value="restoreMBR">Restore only MBR and GPT</Radio>
                <Radio value="restorePartition">Restore volume to selected partition</Radio>
            </Space>
        </Radio.Group>
        </>);
    }

    

    return (<div>
            Select client: <br />
            <Select showSearch loading={!hasClients} onChange={onClientChange} style={{ width: "600px" }} defaultValue="Please select...">
                {clients.map( client => 
                    (<Select.Option key={client} value={client}>{client}</Select.Option>)
                ) }
            </Select>
            <br />
            Select image: <br />
            <Select loading={!hasImages} style={{ width: "600px" }} 
                value={selImage === undefined ? "Please select..." : (selImage.letter + " - " + imageDateTime(selImage, props.props))}
                onChange={(val) => {
                    setSelImage( getSelImageSet().find( image => image.id===parseInt(val) ) as (BackupImage|undefined));
                } }>
                {getSelImageSet().map( image => (
                    <Select.Option key={image.id} value={image.id}>{image.letter} - {imageDateTime(image, props.props)}</Select.Option>
                ))}
            </Select>

            <br />
            <br />
            Select disk to restore to: <br />
            <Select loading={!hasDisks} style={{ width: "600px" }} 
                onChange={ val => setSelDisk(disks.find(disk => disk.path === val) as LocalDisk) }
                value={selDisk === undefined ? "Please select..." : selDisk.path}>
                 {disks.map( disk => 
                    (<Select.Option key={disk.path} value={disk.path}>{disk.model} - size {disk.size} at {disk.path}</Select.Option>)
                ) }
            </Select>
            <br /><br />
            <Checkbox checked={advanced} onChange={advancedChange}>Show advanced restore options</Checkbox>
            {advanced &&
                    renderAdvanced()}
            <br />
            <br />
            <br />
            <Button type="primary" htmlType="submit" onClick={advanceWizard}>
                Review
            </Button>
        { remoteError.length>0 &&
            <Alert message={remoteError} type="error" />
        }
    </div>);
}

export default ConfigRestore;