import { Alert, Button, Form, Input, Spin } from "antd";
import produce from "immer";
import { useState } from "react";
import { useMountEffect } from "./App";
import { WizardComponent, WizardState } from "./WizardState";



function Login(props: WizardComponent) {
    const [tryAnonymousLogin, setTryAnonymousLogin] = useState(true);
    const [isLoading, setIsLoading] = useState(false);
    const [loginError, setLoginError] = useState("");

    useMountEffect(() => {
        if(props.props.username.length>0) {
            setTryAnonymousLogin(false);
            return;
        }

        ( async () => {
        let jdata;
        try {
            const resp = await fetch("x?a=login",
                {method: "POST"})
            jdata = await resp.json();
        } catch(error) {
            setLoginError("Error during anonymous login");
            setTryAnonymousLogin(false);
            return;
        }

        if(jdata["success"]) {
            props.update(produce(props.props, draft => {
                draft.state = WizardState.ConfigRestore;
                draft.max_state = draft.state;
            }));
            return;
        }

        setTryAnonymousLogin(false);
    })();
    });

    const onFinish = (values: any) => {
        const username = values["username"];
        const password = values["password"];

        setIsLoading(true);

        ( async () => {
            let jdata;
            try {
                const resp = await fetch("x?a=login",
                    {method: "POST",
                    body: new URLSearchParams({
                        "has_login_data": "1",
                        "username": username,
                        "password": password
                    }) } );
                jdata = await resp.json();
            } catch(error) {
                setLoginError("Error while submitting login details");
                setIsLoading(false);
                return;
            }

            setIsLoading(false);

            if(!jdata["ok"]) {
                setLoginError("Error while submitting login details -2");
                return;
            }

            if(jdata["success"]) {
                props.update(produce(props.props, draft => {
                    draft.state = WizardState.ConfigRestore;
                    draft.max_state = draft.state;
                    draft.username = username;
                    draft.password = password;
                }));
                return;
            } else {
                setLoginError("Login failed");
            }
        })();
    };

    if (tryAnonymousLogin)
        return (<div>
            <Spin size="large" /> <br /> <br />
            Trying anonymous login...
        </div>);
    else
        return (<div>
            <Form
            name="basic"
            labelCol={{ span: 8 }}
            wrapperCol={{ span: 16 }}
            layout="vertical"
            labelAlign="left"
            initialValues={{ 
                username: props.props.username,
                password: props.props.password
             }}
            onFinish={onFinish}>
            <Form.Item
                label="Username"
                name="username"
                rules={[{ required: true, message: 'Please enter your username' }]}>
                <Input />
            </Form.Item>

            <Form.Item
                label="Password"
                name="password">
                <Input.Password/>
            </Form.Item>

            <Form.Item>
                <Button type="primary" htmlType="submit" loading={isLoading}>
                    Submit
                </Button>
            </Form.Item>
        </Form>
        { loginError.length>0 &&
            <Alert message={loginError} type="error" />
        }
        </div>);
}

export default Login;