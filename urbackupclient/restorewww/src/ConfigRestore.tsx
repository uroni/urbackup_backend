import { Alert, Button, Select } from "antd";
import produce from "immer";
import { useState } from "react";
import { toIsoDateTime, useMountEffect } from "./App";
import { BackupImage, LocalDisk, WizardComponent, WizardState } from "./WizardState";

function ConfigRestore(props: WizardComponent) {
    const [remoteError, setRemoteError] = useState("");
    const [hasClients, setHasClients] = useState(false);
    const [hasImages, setHasImages] = useState(true);
    const [clients, setClients] = useState<string[]>([]);
    const [images, setImages] = useState<BackupImage[]>([]);
    const [selImage, setSelImage] = useState< BackupImage | null>(null);
    const [hasDisks, setHasDisks] = useState(false);
    const [disks, setDisks] = useState<LocalDisk[]>([]);
    const [selDisk, setSelDisk] = useState<LocalDisk | null>(null);

    useMountEffect( () => {
        ( async () => {
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

            try {
                const resp = await fetch("x?a=get_disks",
                    {method: "POST"});
                jdata = await resp.json();
            } catch(error) {
                setRemoteError("Error while getting local disks");
                return;
            }

            setDisks(jdata["disks"]);
            setHasDisks(true);
        })();
    } );

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

        setImages(jdata["images"].map(
            (image: any) => ({...image, "clientname": value}))
        );
        setHasImages(true);
    }

    const advanceWizard = () => {
        if (selImage===null) {
            setRemoteError("Please select an image to restore");
            return;
        }

        if(selDisk===null) {
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


    return (<div>
            Select client: <br />
            <Select showSearch loading={!hasClients} onChange={onClientChange} style={{ width: "600px" }} defaultValue="Please select...">
                {clients.map( client => 
                    (<Select.Option value={client}>{client}</Select.Option>)
                ) }
            </Select>
            <br />
            Select image: <br />
            <Select loading={!hasImages} style={{ width: "600px" }} 
                value={selImage === null ? "Please select..." : (selImage.letter + " - " + toIsoDateTime(new Date(selImage.time_s*1000)))}
                onChange={(val) => {
                    setSelImage( images.find( image => image.id===parseInt(val) ) as BackupImage);
                } }>
                {images.map( image => (
                    <Select.Option value={image.id}>{image.letter} - {toIsoDateTime(new Date(image.time_s*1000))}</Select.Option>
                ))}
            </Select>

            <br />
            <br />
            Select disk to restore to: <br />
            <Select loading={!hasDisks} style={{ width: "600px" }} 
                defaultValue="Please select..."
                onChange={ val => setSelDisk(disks.find(disk => disk.path === val) as LocalDisk) }>
                 {disks.map( disk => 
                    (<Select.Option value={disk.path}>{disk.model} - size {disk.size} at {disk.path}</Select.Option>)
                ) }
            </Select>
            <br />
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