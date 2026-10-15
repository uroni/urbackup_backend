import { Alert, Button, Checkbox, Form, Input } from "antd";
import produce from "immer";
import { useState } from "react";
import { WizardComponent, WizardState } from "./WizardState";

function ConfigureServerConnection(props: WizardComponent) {
    const [configureError, setConfigureError] = useState("");
    const [isLoading, setIsLoading] = useState(false)

    const onFinish = (values: any) => {

        const internetServer = values["internetServer"];
        const serverUrl = values["serverUrl"];
        const serverAuthkey = values["serverAuthkey"];
        const serverProxy =  values["serverProxy"];

        setIsLoading(true);

        (async () => {
            try {
                const resp = await fetch("x?a=configure_server",
                    {method: "POST",
                    body: new URLSearchParams({
                        "active": internetServer ? "1" : "0",
                        "url": serverUrl,
                        "authkey": serverAuthkey,
                        "proxy": serverProxy
                    }) });
                var jdata = await resp.json();
            } catch(error) {
                setConfigureError("Error retrieving data from HTTP server");
            }

            setIsLoading(false);

            if(!jdata["ok"]) {
                setConfigureError("Error configuring client to connect to server");
            }

            props.update(produce(props.props, draft => {
                draft.state = WizardState.WaitForConnection;
                draft.max_state = draft.state;
                draft.internetServer = internetServer;
                draft.serverUrl = serverUrl;
                draft.serverAuthkey = serverAuthkey;
                draft.serverProxy = serverProxy;
            }));
        })();
    }

    return (<div>
        <Form
            name="basic"
            labelCol={{ span: 8 }}
            wrapperCol={{ span: 16 }}
            labelAlign="left"
            layout="vertical"
            initialValues={{ 
                internetServer: true,
                serverUrl: props.props.serverUrl,
                serverAuthkey: props.props.serverAuthkey,
                serverProxy: props.props.serverProxy
             }}
            onFinish={onFinish}
        >
            <Form.Item name="internetServer" valuePropName="checked">
              <Checkbox>Connect to Internet server/active connection</Checkbox>
            </Form.Item>

            <Form.Item
                label="Server URL"
                name="serverUrl"
                rules={[{ required: true, message: 'Please enter the Server URL to connect to' }]}
            >
                <Input placeholder="urbackup://example.com"/>
            </Form.Item>

            <Form.Item
                label="Server restore authentication key"
                name="serverAuthkey"
                rules={[{ required: true, message: 'Please enter the restore authentication key for the server' }]}
            >
                <Input/>
            </Form.Item>

            <Form.Item
                label="Server HTTP proxy"
                name="serverProxy"
                rules={[{ required: false }]}
            >
                <Input placeholder="https://example.com" />
            </Form.Item>
            <Form.Item>
                <Button type="primary" htmlType="submit" loading={isLoading}>
                    Submit
                </Button>
            </Form.Item>
        </Form>
        { configureError.length>0 &&
            <Alert message={configureError} type="error" />
        }
        </div>
    )
}

export default ConfigureServerConnection;