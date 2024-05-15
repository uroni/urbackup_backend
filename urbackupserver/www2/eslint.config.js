import eslint from '@eslint/js';
import tseslint from 'typescript-eslint';
import eslintConfigPrettier from "eslint-config-prettier";
import lingui from 'eslint-plugin-lingui';

export default tseslint.config(
  eslint.configs.recommended,
  ...tseslint.configs.recommended,
  {
    languageOptions: {
        parserOptions: {
            parser: "@typescript-eslint/parser",
            project: "./tsconfig.json",
        },
    },
    ignores: ["dist/", "lingui.config.ts", "vite.config.ts", "eslint.config.js"],
    rules: {
        "@typescript-eslint/no-floating-promises": "error"
    },
    plugins:
    {
        lingui
    }
  },
  eslintConfigPrettier
);