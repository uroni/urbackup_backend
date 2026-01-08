import { ClientSettingState } from "../../../../api/urbackupserver";

export const SETTINGS_SOURCE = {
  GROUP: "group",
  HERE: "here",
  CLIENT: "client",
} as const;

export const USE_VALUES = {
  [SETTINGS_SOURCE.GROUP]: 1, // 1 << 0
  [SETTINGS_SOURCE.HERE]: 2, // 1 << 1
  [SETTINGS_SOURCE.CLIENT]: 4, // 1 << 2
} as const;

export const VALUE_TO_USE: Record<
  number,
  Exclude<keyof ClientSettingState, "use">
> = {
  [USE_VALUES[SETTINGS_SOURCE.GROUP]]: "value_group",
  [USE_VALUES[SETTINGS_SOURCE.HERE]]: "value",
  [USE_VALUES[SETTINGS_SOURCE.CLIENT]]: "value_client",
};

export const FORMATTED_VALUE_TO_USE = {
  value_group: {
    name: "Group",
    desc: "Use setting from group",
  },
  value: {
    name: "Here",
    desc: "Use setting configured here",
  },
  value_client: {
    name: "Client",
    desc: "Use setting configured on client",
  },
} satisfies Record<
  (typeof VALUE_TO_USE)[keyof typeof VALUE_TO_USE],
  { name: string; desc: string }
>;

export interface EnabledState {
  name: string;
  enabled: 0 | 1;
  use: (typeof USE_VALUES)[keyof typeof USE_VALUES];
}

export function getUse(enabledStates: EnabledState[], use = 0) {
  const [current] = enabledStates;

  if (!current) {
    return use;
  }

  const newUse = current.enabled ? (use |= current.use) : use;

  return getUse(enabledStates.slice(1), newUse);
}

export function getEnabledState(use: number): EnabledState[] {
  const state = Object.entries(USE_VALUES).map(([k, v]) => ({
    name: k,
    enabled: use & v ? 1 : (0 as EnabledState["enabled"]),
    use: v,
  }));

  return state;
}
