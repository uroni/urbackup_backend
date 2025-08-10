import { z } from "zod/v4-mini";

export const VALIDATION_MESSAGES = {
  required: (label: string) =>
    `Please enter a value for ${label.toLowerCase()}`,
  numeric: (label: string) =>
    `Please enter a numeric value for ${label.toLowerCase()}`,
  regex: (label: string) => `The format for ${label.toLowerCase()} is invalid`,
} as const;

export const requiredStringValidation = (message?: string) =>
  z.string().check(z.minLength(1, message));
