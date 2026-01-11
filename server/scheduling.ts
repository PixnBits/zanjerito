import { Job } from 'node-schedule';
import { StationList, Station } from './stations';

import * as durationFns from 'duration-fns';
import scheduler from 'node-schedule';

import { addToSchema } from './schema';

const sleep = require('util').promisify(setTimeout);

// avoid water hammer by opening the next valve before closing the previous one
// disabled for any durations less than this overlap
const OVERLAP = 2e3;

const schedulesConfiguration = [
  {
    title: 'Seedlings',
    notes: 'the later stages',
    // parts of the year
    // beginOn: undefined,
    // endAt: undefined,
    // enabled: true,
    starts: [
      '00 23 07 * * 0-5', // 7:23am Sun through Fri
      '00 17 20 * * 6-7,0-4', // 8:17pm Sat through Thurs
    ].concat(process.env.NODE_ENV === 'development' ? [
      `00 ${(new Date()).getMinutes() + 1} ${(new Date()).getHours()} * * *`,
    ] : []),
    itinerary: [
      {
        index: 0,
        duration: 'PT3M',
      },
      {
        index: 1,
        duration: 'PT3M',
      },
      {
        index: 3,
        duration: 'PT2M',
      },
    ],
  },
];

let activeJobs = [];

export default async function setup(stations: StationList, power: Station) {
  // FIXME: parse before starting setup
  // // ensure the configuration is usable (parseable) before replacing the old with the new
  // const parsedConfig = schedulesConfiguration.map(({ schedules, stations }) => {
  //   scheduler.RecurrenceRule
  // });

  const schedules = schedulesConfiguration.map(scheduleConfiguration => {
    const jobs = scheduleConfiguration.starts.map(
      rule => scheduler.scheduleJob(rule, async () => {
        console.info(`time to run schedule ${scheduleConfiguration.title} (${new Date()})`);
        const plan = [];
        let wasCurrentNowPreviousStation = null;
        for await (const { index, duration } of scheduleConfiguration.itinerary) {
          const durationMS = durationFns.toMilliseconds(durationFns.parse(duration));
          const currentStation = stations[index];
          const previousStation = wasCurrentNowPreviousStation;
          if (!previousStation) {
            plan.push({
              action: () => {
                console.info(`running station ${index} for ${duration}`, durationMS, new Date());
                return currentStation.on();
              },
              wait: durationMS,
            });
          } else {
            let firstWait = OVERLAP;
            let secondWait = durationMS - OVERLAP;
            if (secondWait < 0) {
              // duration is less than the overlap
              firstWait = durationMS;
              secondWait = 0;
            }
            plan.push(
              {
                action: () => {
                  console.info(`running station ${index} for ${duration}`, firstWait + secondWait, new Date());
                  return currentStation.on();
                },
                wait: firstWait,
              },
              {
                action: () => {
                  console.info(`turning off station ${stations.indexOf(previousStation)}`, firstWait, secondWait, new Date());
                  return previousStation.off();
                },
                wait: secondWait,
              }
            );
          }

          wasCurrentNowPreviousStation = currentStation;
        }

        if (wasCurrentNowPreviousStation) {
          const currentStation = wasCurrentNowPreviousStation;
          plan.push({
            action: () => {
              console.info(`turning off station ${stations.indexOf(currentStation)}`, 0, new Date());
              return currentStation.off();
            },
            wait: 0,
          });
        }

        if (plan.length > 0) {
          const startingAction = plan[0].action;
          plan[0].action = async () => {
            console.info(`turning on power`, new Date());
            await power.on();
            return await startingAction();
          }
          const finishingAction = plan[plan.length - 1].action;
          plan[plan.length - 1].action = async () => {
            const returnValue = await finishingAction();
            console.info(`turning off power`, new Date());
            await power.off();
            return returnValue;
          }
        }
        console.log('plan', plan);
        console.info(`running schedule ${scheduleConfiguration.title} (${new Date()})`);
        for await (const { action, wait } of plan) {
          await action();
          if (wait > 0) {
            await sleep(wait);
          }
        }
        console.info(`finished schedule ${scheduleConfiguration.title} (${new Date()})`);
      })
    );

    return {
      ...scheduleConfiguration,
      jobs,
    };
  });

  addToSchema(
    `
      extend type Query {
        schedules: [Schedule]!
      }

      type Schedule {
        id: ID!
        title: String!
        notes: String
        starts: [ScheduleStart]!
        itinerary: [ItineraryItem]!
      }

      type ScheduleStart {
        definition: String!
        nextInvocation: String
      }

      type ItineraryItem {
        station: Station!
        duration: String!
      }
    `,
    {
      Query: {
        schedules() { return schedules; },
      },
      Schedule: {
        id(schedule) { return Buffer.from(`$Schedule::${schedules.indexOf(schedule)}`, 'utf8').toString('base64'); },
        starts(schedule) {
          const jobs: Job[] = schedule.jobs;
          return schedule.starts.map((definition: string, index: number) => ({
            definition,
            nextInvocation: jobs[index] ? jobs[index].nextInvocation().toISOString() : null,
          }));
        }
      },
      ItineraryItem: {
        station({ index }) { return stations[index]; }
      }
    }
  );

  return schedules;
}
