import * as React from 'react';
import { useQuery } from 'urql';
import { Link } from 'react-router';

const stationsQuery = `
  query {
    stations {
      id
      title
      notes
    }
  }
`;

export default function Stations() {
  const [{ data, fetching, error }] = useQuery({ query: stationsQuery });

  return (
    <div>
      <h2>Stations</h2>
      <Link to="/stations/edit/new">Add New Station</Link>
      <ul>
        {fetching ? (
          <li><em>Loading...</em></li>
        ) : error ? (
          <li>Error loading: <pre>{error.message}</pre></li>
        ) : data.stations.length > 0 ? (
          data.stations.map(({ id, title, notes }: any) => (
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
