import type { LinguiConfig } from "@lingui/conf";

const config: LinguiConfig = {
  locales: ["en"],
  catalogs: [
    {
      path: "<rootDir>/src/locales/{locale}",
      include: ["src"],
    },
  ],
};

export default config;
