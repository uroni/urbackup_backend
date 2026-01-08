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

export const stringToInt = (message?: string, minLength = 0) =>
  z.pipe(
    z.string().check(z.minLength(minLength, message)),
    z.transform(Number),
  );

export const integerValidation = (
  message?: string,
  options?: { required?: boolean },
) =>
  stringToInt(message, options?.required ? 1 : 0).check(
    z.int(message),
    z.nonnegative(message),
  );
