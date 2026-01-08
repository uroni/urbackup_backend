import { Body2, Title3 } from "@fluentui/react-components";
import { useParams } from "react-router-dom";

import { FormContainer } from "../Form/Form";
import { useSettings } from "../useSettings";
import styles from "./Clients.module.css";

export function Permissions() {
  const { clientId = "" } = useParams();

  const { navitems } = useSettings();

  const client = navitems.clients.find((c) => c.id === +clientId);

  if (!client) {
    return <h1>No client found with ID: {clientId}</h1>;
  }

  return (
    <FormContainer>
      <h1>
        <Body2 className={styles["client-name"]}>{client?.name}</Body2>
        <Title3>Permissions</Title3>
      </h1>
    </FormContainer>
  );
}
