import * as React from 'react';
import { useQuery, useSubscription } from 'urql';

const stationsQuery = `
  query readStations{
    stations {
      title
      id
      notes
    }
  }
`;

const anyStationChange = `
  subscription anyStationChange {
    stationToggled {
      when
      nowOn
      station {
        id
      }
    }
  }
`

// TODO: generate off of the schema
interface Station {
  title: string;
  id: string;
  notes: string;
}

export default function Stations() {
  const [{ data, fetching, error }] = useQuery({ query: stationsQuery });

  const [stationChangeResponse] = useSubscription(
    { query: anyStationChange },
    (stationStates = {}, response) => {
      if (!response) {
        // connection broken, empty response
        return stationStates;
      }
      return {
        ...stationStates,
        [response.stationToggled.station.id]: response.stationToggled.nowOn,
      };
    }
  );

  return (
    <>
      <h2>Stations</h2>
      <ul>
        {
          fetching ? (
            <li><em>Loading...</em></li>
          ) : error ? (
            <li>Error loading: <pre>{error.message}</pre></li>
          ) : data.stations.length > 0 ? (
            data.stations.map(({ id, title, notes }: Station) => (
              <li key={id}>
                {stationChangeResponse.data ? (
                  stationChangeResponse.data[id] ===true ? (
                    'on '
                  ) : stationChangeResponse.data[id] === false ? (
                    'off '
                  ) : (
                    '? '
                  )
                ) : ''}
                <strong>{title}</strong>{' '}
                {notes}
              </li>
            ))
          ) : (
            <li>None yet. Create one in settings.</li>
          )
        }
      </ul>
    </>
  )
}
