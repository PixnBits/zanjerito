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
    channel: 21,
    title: 'Front West',
    notes: 'red',
  },
  {
    channel: 22,
    title: 'Front North',
    notes: 'yellow',
  },
  {
    channel: 23,
    title: 'Drip Line',
    notes: 'blue',
  },
  {
    channel: 24,
    title: 'Front South',
    notes: 'green',
  },
];

const powerEnableConfiguration = {
  channel: 29,
  title: 'Station PSU',
  notes: '24VAC',
};

const STATION_TOGGLED_TOPIC = 'STATION_TOGGLED';
const IS_DEV = process.env.NODE_ENV === 'development' ? true : false;

export default async function setup() {
  // using gpio on a non-Raspberry Pi device fails, making development difficult
  if (!IS_DEV) {
    await Promise.all([
      // power enable
      gpio.setup(powerEnableConfiguration.channel, gpio.DIR_OUT)
        // disable power ASAP
        .then(() => {
          gpio.write(powerEnableConfiguration.channel, false)
        }),
      // all the stations
      ...stationsConfiguration.map(
        ({ channel }) => gpio.setup(channel, gpio.DIR_OUT)
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
        console.log(`GPIO ${channel} true`);
        if (!IS_DEV) {
          gpio.write(channel, true);
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
        console.log(`GPIO ${channel} false`);
        if (!IS_DEV) {
          gpio.write(channel, false);
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
  if (process.env.NODE_ENV === 'development') {
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
    });
  }

  // TODO: change this to an EventEmitter? so that changes can be subscribed to
  return { stations, power };
}
