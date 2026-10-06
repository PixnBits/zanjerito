import { version, homepage } from '../package.json';
import { validateEnvVars } from './configuration';
import setupStations from './stations';
import setupSchedules from './scheduling';
import { listen } from './server';

(async function main(...allArgs) {
  try {
    console.log(`zanjerito v${version} starting up...`);
    console.log(`(${homepage})`);
    validateEnvVars();
    const { stations, power } = await setupStations();
    await setupSchedules(stations, power);
    await listen();
  } catch (err) {
    process.exitCode = 1;
    console.error('Uknown error thrown: ', err);
  }
}(...process.argv));
