import * as React from 'react';
import { useQuery } from 'urql';

const nextWateringQuery = `
  query {
    schedules {
      starts {
        nextInvocation
      }
    }
  }
`;

export default function Home() {
  const [{ data, fetching, error }] = useQuery({ query: nextWateringQuery });

  const nextWatering = React.useMemo(() => {
    if (!data || !data.schedules) return null;
    const times = data.schedules.flatMap(schedule =>
      schedule.starts.map(start => new Date(start.nextInvocation))
    );
    return times.sort((a, b) => a - b)[0];
  }, [data]);

  return (
    <div>
      <h2>Next Watering</h2>
      {fetching ? (
        <p>Loading...</p>
      ) : error ? (
        <p>Error: {error.message}</p>
      ) : nextWatering ? (
        <p>The next watering is scheduled for {nextWatering.toLocaleString()}.</p>
      ) : (
        <p>No watering schedules found.</p>
      )}
      <nav>
        <a href="/stations">View Stations</a>
        <a href="/schedules">View Schedules</a>
      </nav>
    </div>
  );
}
