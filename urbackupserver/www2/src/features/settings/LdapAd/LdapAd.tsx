import { useState } from "react";

import { Banner } from "../../../components/Banner/Banner";
import { useLdapAd } from "./useLdapAd";
import { FormCard, FormContainer } from "../Form/Form";
import {
  ldapAdFormSchema,
  ldapFields,
  type LdapSettingsKey,
} from "./ldapAdForm";
import { FormSection } from "./FormSection";
import { getInitialFormState } from "../Form/utils";

export function LdapAd() {
  const { settings, handleSubmit } = useLdapAd();

  const [initialFormState] = useState(() =>
    getInitialFormState<LdapSettingsKey>(ldapFields, settings),
  );

  return (
    <FormContainer>
      <h1>LDAP/AD</h1>

      <Banner />

      <FormCard>
        <FormSection
          fields={ldapFields}
          schema={ldapAdFormSchema}
          initialFormState={initialFormState}
          onSubmit={handleSubmit}
        />
      </FormCard>
    </FormContainer>
  );
}
