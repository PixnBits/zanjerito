import * as React from 'react';
import { useQuery } from 'urql';
import * as durationFns from 'duration-fns'

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

function friendlyMsDiff(ms: number) {
  if (ms > 23*60*60*1e3) {
    return `${Math.round(ms / (24*60*60*1e3))} day(s)`
  }
  if (ms > 50*60*1e3) {
    return `${Math.round(ms / (60*60*1e3))} hour(s)`
  }
  if (ms >= 50e3) {
    return `${Math.round(ms / 60e3)} minute(s)`
  }
  if (ms >= 750) {
    return `${Math.round(ms / 1e3)} second(s)`
  }
  return `${ms} millisecond(s)`
}

function friendlyDuration(str: string) {
  return friendlyMsDiff(durationFns.toMilliseconds(durationFns.parse(str)));
}

export default function Schedules() {
  const [{ data, fetching, error }] = useQuery({ query });

  const now = Date.now();

  return (
    <>
      <h2>Schedules</h2>
      <ul>
        {
          fetching ? (
            <li><em>Loading...</em></li>
          ) : error ? (
            <li>Error loading: <pre>{error.message}</pre></li>
          ) : data.schedules.length > 0 ? (
            data.schedules.map(({ id, title, notes, starts, itinerary }: Schedule) => (
              <li key={id}>
                <strong>{title}</strong>{' '}
                {notes}
                <br />
                Start times:
                <ul>
                  {
                    starts.length > 0 ? (
                      starts.map(({ definition, nextInvocation }) => (
                        <li>{definition}, <span title={new Date(nextInvocation).toString()}>next invocation in {friendlyMsDiff(new Date(nextInvocation).valueOf() - now)}</span></li>
                      ))
                    ) : (
                      <li>No start times/triggers yet. Add one</li>
                    )
                  }
                </ul>
                Itinerary:
                <ol>
                  {
                    itinerary.length > 0 ? (
                      itinerary.map(({ station, duration }) => (
                        <li title={`${station.id} for ${duration}`}>{station.title}, {friendlyDuration(duration)}</li>
                      ))
                    ) : (
                      <li>No stations/actions yet. Add one</li>
                    )
                  }
                </ol>
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
