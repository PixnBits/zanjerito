import * as React from 'react';
import { useQuery, useSubscription } from 'urql';
import { Link } from 'react-router';

// TODO: generate off of the schema
interface Station {
  title: string;
  id: string;
  notes: string;
}

const stationsQuery = `
  query {
    stations {
      id
      title
      notes
    }
  }
`;

const stationToggledSubscription = `
  subscription {
    stationToggled {
      station {
        id
        title
      }
      when
      nowOn
    }
  }
`;

export default function Stations() {
  const [{ data, fetching, error }] = useQuery({ query: stationsQuery });

  const [subscriptionData, setSubscriptionData] = React.useState<{
    station: { id: string; title: string };
    when: string;
    nowOn: boolean;
  } | null>(null);

  useSubscription(
    { query: stationToggledSubscription },
    (prev, response) => {
      setSubscriptionData(response.stationToggled);
      return response;
    }
  );

  return (
    <div>
      <h2>Stations</h2>
      {subscriptionData && (
        <div className="mt-4">
          <h4>Recent Activity</h4>
          <p>
            Station <strong>{subscriptionData.station.title}</strong> was turned{' '}
            {subscriptionData.nowOn ? 'on' : 'off'} at{' '}
            {new Date(subscriptionData.when).toLocaleString()}.
          </p>
        </div>
      )}
      <Link to="/stations/edit/new">Add New Station</Link>
      <ul>
        {fetching ? (
          <li><em>Loading...</em></li>
        ) : error ? (
          <li>Error loading: <pre>{error.message}</pre></li>
        ) : data.stations.length > 0 ? (
            data.stations.map(({ id, title, notes }: Station) => (
            <li key={id}>
              <strong>{title}</strong> {notes && `- ${notes}`}
              <br />
              <Link to={`/stations/edit/${id}`}>Edit</Link>
            </li>
          ))
        ) : (
          <li>No stations found. Add one to get started.</li>
        )}
      </ul>
    </div>
  );
}
