import { useState } from "react";
import {
  Body1,
  Button,
  Field,
  Input,
  Select,
} from "@fluentui/react-components";
import { AddRegular, DeleteRegular } from "@fluentui/react-icons";
import {
  useMutation,
  useQueryClient,
  useSuspenseQuery,
} from "@tanstack/react-query";
import { useSearchParams } from "react-router-dom";

import type {
  AlertScriptEdit,
  AlertScriptParam,
} from "../../../api/urbackupserver";
import { urbackupServer } from "../../../App";
import { Banner } from "../../../components/Banner/Banner";
import {
  addMessage,
  clearMessages,
} from "../../../components/Banner/messageStore";
import { FormCard, FormContainer } from "../Form/Form";
import { CodeEditor } from "./CodeEditor";
import styles from "./Scripts.module.css";

const PARAM_TYPES = {
  str: "String",
  num: "Floating point number",
  int: "Integer",
  bool: "Boolean",
  choice: "Choice (separated by |)",
  interval: "Time interval (h, min, s)",
};

type Param = Pick<
  AlertScriptParam,
  "name" | "label" | "default_value" | "type"
>;

// Id of a script that isn't saved yet
const NEW_SCRIPT = -1;

/**
 * Editor of the Lua scripts that send the alerts
 */
export function AlertScripts() {
  const [searchParams, setSearchParams] = useSearchParams();
  const id = Number(searchParams.get("id") ?? 1);

  const { data } = useSuspenseQuery({
    queryKey: ["scripts", "alert", id],
    queryFn: () => urbackupServer.getAlertScript(id),
  });

  // Name of an added script, until it is saved
  const [newName, setNewName] = useState<string | null>(null);

  const select = (id: number) => {
    setNewName(null);
    setSearchParams({ id: String(id) });
  };

  const script: AlertScriptEdit =
    newName === null
      ? data
      : {
          ...data,
          scripts: [...data.scripts, { id: NEW_SCRIPT, name: newName }],
          id: NEW_SCRIPT,
          script: "",
          params: [],
        };

  return (
    <FormContainer>
      <h1>Alert scripts</h1>

      <Banner />

      <AlertScriptForm
        key={script.id}
        data={script}
        onSelect={select}
        onAdd={setNewName}
      />
    </FormContainer>
  );
}

function AlertScriptForm({
  data,
  onSelect,
  onAdd,
}: {
  data: AlertScriptEdit;
  onSelect: (id: number) => void;
  onAdd: (name: string) => void;
}) {
  const name = data.scripts.find((s) => s.id === data.id)?.name ?? "";

  // Like the old web interface: the default script and the built-in ones
  // can't be changed
  const readOnly = data.id === 1 || data.id % 100000 === 0;

  const [params, setParams] = useState<Param[]>(data.params);
  const [script, setScript] = useState(data.script);

  const queryClient = useQueryClient();

  const mutation = useMutation({
    mutationFn: () =>
      urbackupServer.saveAlertScript(data.id, name, params, script),
    onSuccess: async (saved) => {
      clearMessages();
      addMessage({ intent: "success", text: "Saved the alert script." });
      // The alert settings list the scripts and their parameters
      await queryClient.invalidateQueries({ queryKey: ["settings"] });
      await queryClient.invalidateQueries({ queryKey: ["scripts"] });
      onSelect(saved.id);
    },
    onError: () => {
      clearMessages();
      addMessage({ intent: "error", text: "Saving the alert script failed." });
    },
  });

  const add = () => {
    const newName = window.prompt("Name of the new alert script");
    if (newName) {
      onAdd(newName);
    }
  };

  const remove = async () => {
    if (!window.confirm(`Remove the alert script "${name}"?`)) {
      return;
    }

    await urbackupServer.removeAlertScript(data.id);
    await queryClient.invalidateQueries({ queryKey: ["settings"] });
    await queryClient.invalidateQueries({ queryKey: ["scripts"] });
    onSelect(1);
  };

  const setParam = (i: number, update: Partial<Param>) =>
    setParams((p) =>
      p.map((param, j) => (j === i ? { ...param, ...update } : param)),
    );

  return (
    <FormCard>
      <form
        className="flow"
        onSubmit={(e) => {
          e.preventDefault();
          mutation.mutate();
        }}
      >
        <div className={styles.toolbar}>
          <Field label="Alert script">
            <Select
              value={String(data.id)}
              onChange={(_, d) => onSelect(Number(d.value))}
            >
              {data.scripts.map((s) => (
                <option key={s.id} value={String(s.id)}>
                  {s.name}
                </option>
              ))}
            </Select>
          </Field>
          <Button icon={<AddRegular />} onClick={add}>
            Add
          </Button>
          <Button
            icon={<DeleteRegular />}
            onClick={remove}
            disabled={readOnly || data.id === NEW_SCRIPT}
          >
            Remove
          </Button>
        </div>

        {readOnly && (
          <Body1 as="p" block>
            This script is part of UrBackup and can't be changed. Add a script
            to write your own.
          </Body1>
        )}

        <h2>Parameters</h2>
        {params.map((param, i) => (
          <fieldset key={i} className={styles.param} disabled={readOnly}>
            <legend className="visually-hidden">Parameter {i + 1}</legend>
            <Field label="Name">
              <Input
                value={param.name}
                onChange={(_, d) => setParam(i, { name: d.value })}
              />
            </Field>
            <Field label="Label">
              <Input
                value={param.label}
                onChange={(_, d) => setParam(i, { label: d.value })}
              />
            </Field>
            <Field label="Default value">
              <Input
                value={param.default_value}
                onChange={(_, d) => setParam(i, { default_value: d.value })}
              />
            </Field>
            <Field label="Type">
              <Select
                value={param.type}
                onChange={(_, d) => setParam(i, { type: d.value })}
              >
                {Object.entries(PARAM_TYPES).map(([k, label]) => (
                  <option key={k} value={k}>
                    {label}
                  </option>
                ))}
              </Select>
            </Field>
            <Button
              appearance="subtle"
              icon={<DeleteRegular />}
              aria-label={`Remove parameter ${param.name}`}
              onClick={() => setParams((p) => p.filter((_, j) => j !== i))}
            />
          </fieldset>
        ))}
        <div>
          <Button
            icon={<AddRegular />}
            disabled={readOnly}
            onClick={() =>
              setParams((p) => [
                ...p,
                { name: "", label: "", default_value: "", type: "str" },
              ])
            }
          >
            Add parameter
          </Button>
        </div>

        <Field label={<h2>Script (Lua)</h2>}>
          <CodeEditor
            label="Script (Lua)"
            value={script}
            readOnly={readOnly}
            rows={25}
            onChange={setScript}
          />
        </Field>

        <div>
          <Button type="submit" appearance="primary" disabled={readOnly}>
            Save
          </Button>
        </div>
      </form>
    </FormCard>
  );
}
