import { promise as gpio } from 'rpi-gpio';

import { addToSchema } from './schema';

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

/*
function buildStation(station) {
  const { channel } = station;
  return {
    title: station.title,
    notes: station.notes,
    on: () => gpio.write(channel, true),
    off: () => gpio.write(channel, false),
  };
}

export default async function setup() {
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

  return {
    stations: stationsConfiguration.map(buildStation),
    power: buildStation(powerEnableConfiguration),
  };
}
/*/
export default async function setup() {
  await Promise.resolve();

  function buildStation(station: StationConfiguration) {
    const { channel } = station;
    return {
      title: station.title,
      notes: station.notes,
      // currentlyOn: undefined,
      on: async () => console.log(`GPIO ${channel} true`),
      off: async () => console.log(`GPIO ${channel} false`),
    };
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
    `,
    {
      Query: {
        stations() {
          return stations
        },
      },
      Station: {
        id(station) { return Buffer.from(`$Station::${stations.indexOf(station)}`, 'utf8').toString('base64'); }
      }
    }
  );

  // TODO: change this to an EventEmitter so that changes can be subscribed to
  return { stations, power };
}
//*/
