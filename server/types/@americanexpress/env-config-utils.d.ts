declare module '@americanexpress/env-config-utils' {

  interface EnvVarPreprocessConfig {
    name: string;
    normalize?(value: string): any;
    valid?: any[];
    defaultValue: string | (() => any);
    validate?(value: string): any;
  }

  function preprocessEnvVar(config: EnvVarPreprocessConfig): void;
}
