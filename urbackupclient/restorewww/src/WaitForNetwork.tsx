import { Spin } from "antd";
import { WizardComponent} from "./WizardState";


function WaitForNetwork(props: WizardComponent) {
    
    return (
        <div>
            <Spin size="large" /> <br /> <br />
            Waiting for network to become available...
        </div>
    )
}

export default WaitForNetwork;
