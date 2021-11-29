import { preprocessEnvVar } from '@americanexpress/env-config-utils';

export function validateEnvVars() {
  preprocessEnvVar({
    name: 'HTTP_PORT',
    normalize: (input: string) => {
      const parsed = parseInt(input, 10);
      if (Number.isNaN(parsed) || `${parsed}` !== input) {
        throw new Error(`environment variable HTTP_PORT needs to be a valid integer, given "${input}"`);
      } else {
        return parsed;
      }
    },
    defaultValue: () => '3000',
  });
}
