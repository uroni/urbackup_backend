import { Alert, Button } from "antd";
import produce from "immer";
import { imageDateTime } from "./ConfigRestore";
import { WizardComponent, WizardState } from "./WizardState";


function ReviewRestore(props: WizardComponent) {

    const startRestore = async () => {
        props.update(produce(props.props, draft => {
            draft.state = WizardState.Restoring;
            draft.max_state = draft.state;
            draft.disableMenu=true;
        }));
    }


    return (<div>
        Please review before starting restore. <br />
        <Alert type="warning" message="Data on the destination disk will be overwritten!" />
        <br />
        {props.props.restoreOnlyMBR ? "MBR and GPT" : "Image"} to restore: <br />
        <strong>{props.props.restoreOnlyMBR ? "MBR and GPT" : "Image"} of client {props.props.restoreImage.clientname} of volume {props.props.restoreImage.letter} at {imageDateTime(props.props.restoreImage, props.props)}</strong>

        <br />
        <br />
        Restore to {props.props.restoreToPartition ? "partition" : "disk"}:<br />
        <strong>{props.props.restoreToDisk.model} size {props.props.restoreToDisk.size} (path {props.props.restoreToDisk.path} maj:min {props.props.restoreToDisk.maj_min})</strong>

        <br />
        <br />        
        <Button onClick={
            () => {
                props.update(produce(props.props, draft => {
                    draft.state = WizardState.ConfigRestore;
                }))
            }
        }>Back</Button>
        <Button type="primary" danger onClick={startRestore} style={{marginLeft: "10px"}}>
            Start restore</Button>

    </div>);
}

export default ReviewRestore;