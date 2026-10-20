import { useState } from "react";
import {
  Body1,
  Button,
  Field,
  Input,
  Link,
  MessageBar,
  MessageBarBody,
  Radio,
  RadioGroup,
} from "@fluentui/react-components";
import { useQueryClient, useSuspenseQuery } from "@tanstack/react-query";
import { useNavigate } from "react-router-dom";

import type { AddClientResult } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";
import styles from "./AddClient.module.css";

/**
 * Adds an Internet/active client, or a hostname to discover a local/passive
 * client at, like the "Add client" page of the old web interface
 */
export function AddClient() {
  const { data: status } = useSuspenseQuery({
    queryKey: ["status"],
    queryFn: urbackupServer.status,
  });

  const [kind, setKind] = useState<"internet" | "hostname">("internet");
  const [name, setName] = useState("");
  const [error, setError] = useState("");
  const [added, setAdded] = useState<AddClientResult | null>(null);

  const queryClient = useQueryClient();
  const navigate = useNavigate();

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();

    if (name.trim() === "") {
      return setError(
        kind === "internet"
          ? "Please enter a name for the client"
          : "Please enter the IP or hostname of the client",
      );
    }

    try {
      if (kind === "hostname") {
        await urbackupServer.addExtraClient(name.trim());
        await queryClient.invalidateQueries({ queryKey: ["status"] });
        return navigate("/status");
      }

      const result = await urbackupServer.addInternetClient(name.trim());

      if (result.already_exists) {
        return setError("A client with this name already exists");
      }

      await queryClient.invalidateQueries({ queryKey: ["status"] });
      setAdded(result);
    } catch {
      setError("Adding the client failed");
    }
  };

  if (added) {
    return <ClientAdded client={added} />;
  }

  return (
    <div className={`${styles.page} flow`}>
      <h1>Add client</h1>

      <MessageBar intent="info" layout="multiline">
        <MessageBarBody className="flow">
          <p>
            UrBackup automatically discovers clients in your local network. If
            the server is in the same sub-network as the client just install the
            client and wait for it to be discovered. Download the client from{" "}
            <Link href="https://www.urbackup.org/download.html" target="_blank">
              www.urbackup.org
            </Link>
            .
          </p>
          <p>
            If you want a client to use multiple backup servers this server's
            identity is: <code>{status.server_identity}</code>
          </p>
          <p>
            For security reasons check/add following line in the file
            server_idents.txt on your client:
          </p>
          <pre className={styles.code}>{status.server_pubkey}</pre>
        </MessageBarBody>
      </MessageBar>

      <form className="flow" onSubmit={submit}>
        <RadioGroup
          value={kind}
          onChange={(_, d) => {
            setKind(d.value as typeof kind);
            setError("");
          }}
        >
          <Radio value="internet" label="Add new Internet/active client" />
          <Radio
            value="hostname"
            label="Discover new local/passive client across networks"
          />
        </RadioGroup>

        <Field
          label={
            kind === "internet"
              ? "Name of new Internet/active client"
              : "IP/hostname of client to be discovered"
          }
          validationMessage={error || undefined}
        >
          <Input
            value={name}
            onChange={(_, d) => {
              setName(d.value);
              setError("");
            }}
          />
        </Field>

        <div className="cluster">
          <Button onClick={() => navigate("/status")}>Cancel</Button>
          <Button type="submit" appearance="primary">
            Add client
          </Button>
        </div>
      </form>
    </div>
  );
}

function ClientAdded({ client }: { client: AddClientResult }) {
  const download = (os: "windows" | "linux") =>
    urbackupServer.downloadClientURL(
      client.new_clientid,
      client.new_authkey,
      os,
    );

  return (
    <div className={`${styles.page} flow`}>
      <h1>Client added successfully</h1>

      <Body1 as="p" block>
        Added new client with name: <strong>{client.new_clientname}</strong>
      </Body1>
      <Body1 as="p" block>
        Default authentication key: <code>{client.new_authkey}</code>
      </Body1>

      <ul className="flow">
        <li>
          <Link href={download("windows")}>
            Download preconfigured client installer for Windows
          </Link>
        </li>
        <li className="flow">
          <Link href={download("linux")}>
            Download preconfigured client installer for Linux
          </Link>
          <Body1 as="p" block>
            Install it directly in the terminal via:
          </Body1>
          <pre className={styles.code}>
            {`TF=\`mktemp\` && wget "${download("linux")}" -O $TF && sudo sh $TF; rm -f $TF`}
          </pre>
        </li>
        <li>
          <Body1 as="p" block>
            Alternatively install the client from{" "}
            <Link href="https://www.urbackup.org/download.html" target="_blank">
              www.urbackup.org
            </Link>{" "}
            and enter the server address, the client name and the authentication
            key in the Internet settings of the client.
          </Body1>
        </li>
      </ul>
    </div>
  );
}
