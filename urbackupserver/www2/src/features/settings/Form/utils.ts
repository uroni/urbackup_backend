import type { SettingState } from "../../../api/urbackupserver";
import type { Field } from "../Form/types";

export function getInitialFormState<T extends string>(
  fields: Field<T>[],
  settings: Record<T, SettingState["value"]>,
) {
  return fields.reduce(
    (all, f) => ({ ...all, [f.name]: transformValue(settings[f.name]) }),
    {} as typeof settings,
  );
}

function transformValue(value: SettingState["value"]) {
  return String(value) ?? "";
}
