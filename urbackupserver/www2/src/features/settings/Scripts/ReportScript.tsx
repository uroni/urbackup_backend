import { useState } from "react";
import { Button, Field } from "@fluentui/react-components";
import {
  useMutation,
  useQueryClient,
  useSuspenseQuery,
} from "@tanstack/react-query";

import { urbackupServer } from "../../../App";
import { Banner } from "../../../components/Banner/Banner";
import {
  addMessage,
  clearMessages,
} from "../../../components/Banner/messageStore";
import { FormCard, FormContainer } from "../Form/Form";
import { CodeEditor } from "./CodeEditor";

/**
 * Editor of the Lua script that formats the log report mails
 */
export function ReportScript() {
  const { data } = useSuspenseQuery({
    queryKey: ["scripts", "report"],
    queryFn: urbackupServer.getReportScript,
  });

  const [script, setScript] = useState(data.script);

  const queryClient = useQueryClient();

  const mutation = useMutation({
    mutationFn: () => urbackupServer.saveReportScript(script),
    onSuccess: () => {
      clearMessages();
      addMessage({ intent: "success", text: "Saved the report script." });
      return queryClient.invalidateQueries({ queryKey: ["scripts", "report"] });
    },
    onError: () => {
      clearMessages();
      addMessage({ intent: "error", text: "Saving the report script failed." });
    },
  });

  return (
    <FormContainer>
      <h1>Report script</h1>

      <Banner />

      <FormCard>
        <form
          className="flow"
          onSubmit={(e) => {
            e.preventDefault();
            mutation.mutate();
          }}
        >
          <Field label="Script (Lua)">
            <CodeEditor
              label="Script (Lua)"
              value={script}
              rows={30}
              onChange={setScript}
            />
          </Field>
          <div>
            <Button type="submit" appearance="primary">
              Save
            </Button>
          </div>
        </form>
      </FormCard>
    </FormContainer>
  );
}
