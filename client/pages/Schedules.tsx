import * as React from 'react';
import { useQuery } from 'urql';
import { Link } from 'react-router';
import * as durationFns from 'duration-fns';
import cronstrue from 'cronstrue';

// TODO: generate off of the schema
interface Schedule {
  id: string;
  title: string;
  notes: string;
  starts: [ScheduleStart];
  itinerary: [ScheduleAction]
}

interface ScheduleStart {
  definition: string;
  nextInvocation: string;
}

interface ScheduleAction {
  station: Station;
  duration: string;
}

// TODO: generate off of the schema
interface Station {
  title: string;
  id: string;
  // notes: string;
}

const query = `
  query {
    schedules {
      id
      title
      notes
      starts {
        definition
        nextInvocation
      }
      itinerary {
        station {
          id
          title
        }
        duration
      }
    }
  }
`;

function friendlyMsDiff(ms: number) {
  if (ms > 23 * 60 * 60 * 1e3) {
    return `${Math.round(ms / (24 * 60 * 60 * 1e3))} day(s)`;
  }
  if (ms > 50 * 60 * 1e3) {
    return `${Math.round(ms / (60 * 60 * 1e3))} hour(s)`;
  }
  if (ms >= 50e3) {
    return `${Math.round(ms / 60e3)} minute(s)`;
  }
  if (ms >= 750) {
    return `${Math.round(ms / 1e3)} second(s)`;
  }
  return `${ms} millisecond(s)`;
}

function friendlyDuration(str: string) {
  return friendlyMsDiff(durationFns.toMilliseconds(durationFns.parse(str)));
}

function friendlyCron(cron: string) {
  try {
    return cronstrue.toString(cron);
  } catch {
    return `Advanced: ${cron}`
  }
}

export default function Schedules() {
  const [{ data, fetching, error }] = useQuery({ query });

  const now = Date.now();

  return (
    <div>
      <h2>Schedules</h2>
      <Link to="/schedules/edit/new">Add New Schedule</Link>
      <ul>
        {fetching ? (
          <li><em>Loading...</em></li>
        ) : error ? (
          <li>Error loading: <pre>{error.message}</pre></li>
        ) : data.schedules.length > 0 ? (
          data.schedules.map(({ id, title, notes, starts, itinerary }: Schedule) => (
            <li key={id}>
              <strong>{title}</strong> {notes}
              <br />
              Start times:
              <ul>
                {starts.length > 0 ? (
                  starts.map(({ definition, nextInvocation }: ScheduleStart, index: number) => (
                    <li key={index}>
                      <span title={definition}>
                        {friendlyCron(definition)} 
                      </span>,{' '}
                      <span title={new Date(nextInvocation).toString()}>
                        next invocation in {friendlyMsDiff(new Date(nextInvocation).valueOf() - now)}
                      </span>
                    </li>
                  ))
                ) : (
                  <li>No start times/triggers yet. Add one.</li>
                )}
              </ul>
              Itinerary:
              <ol>
                {itinerary.length > 0 ? (
                  itinerary.map(({ station, duration }: ScheduleAction, index: number) => (
                    <li key={index} title={`${station.id} for ${duration}`}>
                      {station.title}, {friendlyDuration(duration)}
                    </li>
                  ))
                ) : (
                  <li>No stations/actions yet. Add one.</li>
                )}
              </ol>
              <Link to={`/schedules/edit/${id}`}>Edit</Link>
            </li>
          ))
        ) : (
          <li>None yet. Create one in settings.</li>
        )}
      </ul>
    </div>
  );
}
