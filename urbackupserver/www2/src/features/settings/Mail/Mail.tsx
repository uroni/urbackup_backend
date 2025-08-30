import { useState } from "react";

import { mailFields, mailFormSchema, type MailSettingsKey } from "./mailForm";
import { Banner } from "../../../components/Banner/Banner";
import { useMail } from "./useMail";
import { TestMail } from "./TestMail";
import { FormCard, FormContainer } from "../Form/Form";
import { FormSection } from "../LdapAd/FormSection";
import { getInitialFormState } from "../Form/utils";

export function Mail() {
  const { settings, handleSubmit, sendTestMail } = useMail();

  const [initialFormState] = useState(() =>
    getInitialFormState<MailSettingsKey>(mailFields, settings),
  );

  return (
    <FormContainer>
      <h1>Mail</h1>

      <Banner />

      <FormCard>
        <FormSection
          fields={mailFields}
          schema={mailFormSchema}
          initialFormState={initialFormState}
          onSubmit={handleSubmit}
        />
      </FormCard>

      <TestMail onSubmit={sendTestMail} />
    </FormContainer>
  );
}
