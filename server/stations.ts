import { promise as gpio } from 'rpi-gpio';

import { addToSchema, subscriptionEmitter } from './schema';

interface StationConfiguration {
  channel: number,
  title: string,
  notes: string,
};

export interface Station {
  title: string,
  notes?: string,
  on(): Promise<void>,
  off(): Promise<void>,
}

export interface StationList extends Array<Station> {
  allOff(): Promise<void[]>,
}

const stationsConfiguration = [
  {
    // wiringPi 21, physical 29
    channel: 29,
    title: 'Front West',
    notes: 'red, wiringPi 21, physical 29',
  },
  {
    // wiringPi 22, physical 31
    channel: 31,
    title: 'Front North',
    notes: 'yellow, wiringPi 22, physical 31',
  },
  {
    // wiringPi 23, physical 33
    channel: 33,
    title: 'Drip Line',
    notes: 'blue, wiringPi 23, physical 33',
  },
  {
    // wiringPi 24, physical 35
    channel: 35,
    title: 'Front South',
    notes: 'green, wiringPi 24, physical 35',
  },
  {
    // wiringPi 25, physical 37
    channel: 37,
    title: '25, physical 37',
    notes: 'unconnected',
  },
  {
    // wiringPi 27, physical 36
    channel: 36,
    title: 'wiringPi 27, physical 36',
    notes: 'unconnected',
  },
  {
    // wiringPi 28, physical 38
    channel: 38,
    title: 'wiringPi 28, physical 38',
    notes: 'unconnected',
  },
];

const powerEnableConfiguration = {
  // wiringPi 29, physical 40
  channel: 40,
  title: 'Station PSU',
  notes: '24VAC',
};

const STATION_TOGGLED_TOPIC = 'STATION_TOGGLED';
const IS_DEV = process.env.NODE_ENV === 'development' ? true : false;

export default async function setup() {
  // using gpio on a non-Raspberry Pi device fails, making development difficult
  if (!IS_DEV) {
    // MODE_RPI is the default and is the physical pin number
    // note that the `gpio` CLI tool instead uses wiringPi by default
    // use `gpio readall` to see the differences
    await Promise.all([
      // power enable (DIR_OUT) but also off (DIR_HIGH as the relay board inverts)
      // disabling power ASAP
      gpio.setup(powerEnableConfiguration.channel, gpio.DIR_HIGH),
      // all the stations
      ...stationsConfiguration.map(
        ({ channel }) => gpio.setup(channel, gpio.DIR_HIGH)
      ),
    ]);
  }

  function buildStation(stationConfiguration: StationConfiguration) {
    const { channel } = stationConfiguration;
    const station = {
      title: stationConfiguration.title,
      notes: stationConfiguration.notes,
      // currentlyOn: undefined,
      on: async () => {
        console.log(`GPIO ${channel} on (false)`);
        if (!IS_DEV) {
          gpio.write(channel, false);
        }
        subscriptionEmitter.emit({
          topic: STATION_TOGGLED_TOPIC,
          payload: {
            stationToggled: {
              when: new Date().toISOString(),
              station,
              nowOn: true,
            }
          }
        });
      },
      off: async () => {
        console.log(`GPIO ${channel} off (true)`);
        if (!IS_DEV) {
          gpio.write(channel, true);
        }
        subscriptionEmitter.emit({
          topic: STATION_TOGGLED_TOPIC,
          payload: {
            stationToggled: {
              when: new Date().toISOString(),
              station,
              nowOn: false,
            }
          }
        });
      },
    };
    return station;
  }

  const stations: StationList = Object.assign(
    stationsConfiguration.map(buildStation),
    {
      allOff: () => Promise.all(stations.map(station => station.off())),
    }
  );

  const power: Station = buildStation(powerEnableConfiguration);

  await Promise.all([
    power.off(),
    stations.allOff(),
  ]);

  addToSchema(
    `
      extend type Query {
        stations: [Station]!
      }

      type Station {
        id: ID!
        title: String!
        notes: String
      }

      extend type Subscription {
        # not necessarily the desired interface, more to prove out this can work
        stationToggled: StationEvent
      }

      type StationEvent {
        station: Station!
        when: String # Date Time
        # should be part of the Station state?
        nowOn: Boolean
      }
    `,
    {
      Query: {
        stations() {
          return stations
        },
      },
      Station: {
        // potentially hot path, measure to see and maybe change the data structure a bit to save CPU?
        id(station) {
          const index = station === power ? 'power' : stations.indexOf(station);
          return Buffer.from(`$Station::${index}`, 'utf8').toString('base64');
        }
      },
      Subscription: {
        stationToggled: {
          subscribe(root, args, { pubsub }) {
            return pubsub.subscribe(STATION_TOGGLED_TOPIC);
          }
        }
      }
    }
  );

  // need to write functional tests instead, but until then
  if (IS_DEV) {
    [
      // [0, 4.5e3],
      [1, 10e3],
      // [2, 3e3],
    ].forEach(([index, interval]) => {
      let toggle = false;
      setInterval(() => {
        toggle = !toggle;
        stations[index][toggle ? 'on' : 'off']();
      }, interval);
      console.log(`set up ${index} to toggle every ${interval}ms`);
    });
  }

  // TODO: change this to an EventEmitter? so that changes can be subscribed to
  return { stations, power };
}
