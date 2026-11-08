import type { InputProps } from "@fluentui/react-components";
import type { ZodMiniType } from "zod/v4-mini";

export interface BaseField<Names> {
  name: Names;
  type: "checkbox" | "number" | "text" | "password" | "email" | "custom";
  validation?: (options?: { label?: string }) => ZodMiniType;
  description?: React.ReactNode;
  hint?: string;
  inputProps?: InputProps & {
    "data-field-width": string;
  };
  transformer?: {
    ui: (v: number) => number | string;
    api: (v: number) => number | string;
  };
}

export interface Field<Name> extends BaseField<Name> {
  label: string;
}
