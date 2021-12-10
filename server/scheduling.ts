import { Job } from 'node-schedule';
import { StationList, Station } from './stations';

import * as durationFns from 'duration-fns';
import scheduler from 'node-schedule';

import { addToSchema } from './schema';

const sleep = require('util').promisify(setTimeout);

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
      // runs during development
      `00 ${(new Date()).getMinutes() + 1} ${(new Date()).getHours()} * * *`,
    ],
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
        console.info(`running schedule ${scheduleConfiguration.title} (${new Date()})`);
        await power.on();
        for await (const { index, duration } of scheduleConfiguration.itinerary) {
          const parsedDuration = durationFns.parse(duration);
          console.info(`running station ${index} for ${duration}`, parsedDuration, new Date());
          stations[index].on();
          await sleep(durationFns.toMilliseconds(parsedDuration));
          stations[index].off();
        }
        await power.off();
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
