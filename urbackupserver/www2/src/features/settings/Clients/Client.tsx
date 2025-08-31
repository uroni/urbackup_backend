import { Body2 } from "@fluentui/react-components";
import { useParams } from "react-router-dom";

import { FormCard, FormContainer } from "../Form/Form";
import { FormSection } from "../Form/FormSection";
import { mailFields, mailFormSchema } from "../Mail/mailForm";
import { useSettings } from "../useSettings";
import styles from "./Clients.module.css";

export function Client() {
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
        <div>Backups</div>
      </h1>

      <FormCard>
        <FormSection
          fields={mailFields}
          schema={mailFormSchema}
          initialFormState={{}}
          onSubmit={() => null}
        />
      </FormCard>
    </FormContainer>
  );
}
