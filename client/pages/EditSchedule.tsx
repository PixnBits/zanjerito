import * as React from 'react';
import { useParams, useNavigate } from 'react-router';
import { useQuery, useMutation } from 'urql';
import cronstrue from 'cronstrue';
import * as durationFns from 'duration-fns';

const scheduleQuery = `
  query ($id: ID!) {
    schedule(id: $id) {
      id
      title
      notes
      starts {
        definition
      }
      itinerary {
        station {
          id
          title
        }
        duration
      }
    }
    stations {
      id
      title
    }
  }
`;

const saveScheduleMutation = `
  mutation ($id: ID, $title: String!, $notes: String, $starts: [String!]!, $itinerary: [ItineraryInput!]!) {
    saveSchedule(id: $id, title: $title, notes: $notes, starts: $starts, itinerary: $itinerary) {
      id
      title
      notes
    }
  }
`;

function friendlyCron(cron: string) {
  try {
    return cronstrue.toString(cron);
  } catch {
    return 'Invalid cron expression';
  }
}

function friendlyDuration(isoDuration: string) {
  try {
    const duration = durationFns.parse(isoDuration);
    const parts: string[] = [];
    if (duration.days) parts.push(`${duration.days} day(s)`);
    if (duration.hours) parts.push(`${duration.hours} hour(s)`);
    if (duration.minutes) parts.push(`${duration.minutes} minute(s)`);
    if (duration.seconds) parts.push(`${duration.seconds} second(s)`);
    return parts.join(', ') || '0 seconds';
  } catch {
    return 'Invalid duration';
  }
}

function isValidCron(cron: string) {
  try {
    cronstrue.toString(cron);
    return true;
  } catch {
    return false;
  }
}

function isValidDuration(isoDuration: string) {
  try {
    durationFns.parse(isoDuration);
    return true;
  } catch {
    return false;
  }
}

export default function EditSchedule() {
  const { id } = useParams();
  const navigate = useNavigate();
  const [schedule, setSchedule] = React.useState({
    title: '',
    notes: '',
    starts: [''],
    itinerary: [{ stationId: '', duration: '' }],
  });

  const [{ data, fetching, error }] = useQuery({
    query: scheduleQuery,
    variables: { id },
    pause: !id, // Skip query if creating a new schedule
  });

  const [, saveSchedule] = useMutation(saveScheduleMutation);

  React.useEffect(() => {
    if (data?.schedule) {
      setSchedule({
        title: data.schedule.title,
        notes: data.schedule.notes,
        starts: data.schedule.starts.map((start: any) => start.definition),
        itinerary: data.schedule.itinerary.map((item: any) => ({
          stationId: item.station.id,
          duration: item.duration,
        })),
      });
    }
  }, [data]);

  const handleChange = (e: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>) => {
    const { name, value } = e.target;
    setSchedule((prev) => ({ ...prev, [name]: value }));
  };

  const handleStartsChange = (index: number, value: string) => {
    setSchedule((prev) => {
      const starts = [...prev.starts];
      starts[index] = value;
      return { ...prev, starts };
    });
  };

  const handleItineraryChange = (index: number, field: string, value: string) => {
    setSchedule((prev) => {
      const itinerary = [...prev.itinerary];
      itinerary[index] = { ...itinerary[index], [field]: value };
      return { ...prev, itinerary };
    });
  };

  const addStartTime = () => {
    setSchedule((prev) => ({
      ...prev,
      starts: [...prev.starts, ''],
    }));
  };

  const removeStartTime = (index: number) => {
    setSchedule((prev) => {
      const starts = [...prev.starts];
      starts.splice(index, 1);
      return { ...prev, starts };
    });
  };

  const addItineraryEntry = () => {
    setSchedule((prev) => ({
      ...prev,
      itinerary: [...prev.itinerary, { stationId: '', duration: '' }],
    }));
  };

  const removeItineraryEntry = (index: number) => {
    setSchedule((prev) => {
      const itinerary = [...prev.itinerary];
      itinerary.splice(index, 1);
      return { ...prev, itinerary };
    });
  };

  const moveItineraryEntry = (fromIndex: number, toIndex: number) => {
    setSchedule((prev) => {
      const itinerary = [...prev.itinerary];
      const [movedItem] = itinerary.splice(fromIndex, 1);
      itinerary.splice(toIndex, 0, movedItem);
      return { ...prev, itinerary };
    });
  };

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    // Validate starts
    const invalidStarts = schedule.starts.some((start) => !isValidCron(start));
    if (invalidStarts) {
      alert('One or more start times are invalid. Please fix them before saving.');
      return;
    }

    // Validate itinerary durations
    const invalidDurations = schedule.itinerary.some((item) => !isValidDuration(item.duration));
    if (invalidDurations) {
      alert('One or more itinerary durations are invalid. Please fix them before saving.');
      return;
    }

    await saveSchedule({
      id,
      title: schedule.title,
      notes: schedule.notes,
      starts: schedule.starts,
      itinerary: schedule.itinerary.map((item) => ({
        stationId: item.stationId,
        duration: item.duration,
      })),
    });
    navigate('/schedules');
  };

  if (fetching) return <p>Loading...</p>;
  if (error) return <p>Error: {error.message}</p>;

  return (
    <div>
      <h2>{id ? 'Edit Schedule' : 'Add New Schedule'}</h2>
      <form onSubmit={handleSubmit}>
        <div>
          <label>
            Title:
            <input
              type="text"
              name="title"
              value={schedule.title}
              onChange={handleChange}
              required
            />
          </label>
        </div>
        <div>
          <label>
            Notes:
            <textarea
              name="notes"
              value={schedule.notes}
              onChange={handleChange}
            />
          </label>
        </div>
        <div>
          <label>Start Times:</label>
          {schedule.starts.map((start, index) => (
            <div key={index}>
              <input
                type="text"
                value={start}
                onChange={(e) => handleStartsChange(index, e.target.value)}
              />
              <span style={{ marginLeft: '10px', fontStyle: 'italic', color: isValidCron(start) ? 'inherit' : 'red' }}>
                {friendlyCron(start)}
              </span>
              <button type="button" onClick={() => removeStartTime(index)}>Remove</button>
            </div>
          ))}
          <button type="button" onClick={addStartTime}>Add Start Time</button>
        </div>
        <div>
          <label>Itinerary:</label>
          {schedule.itinerary.map((item, index) => (
            <div key={index}>
              <select
                value={item.stationId}
                onChange={(e) => handleItineraryChange(index, 'stationId', e.target.value)}
              >
                <option value="">Select Station</option>
                {data?.stations.map((station: any) => (
                  <option key={station.id} value={station.id}>
                    {station.title}
                  </option>
                ))}
              </select>
              <input
                type="text"
                placeholder="Duration (e.g., PT5M)"
                value={item.duration}
                onChange={(e) => handleItineraryChange(index, 'duration', e.target.value)}
              />
              <span style={{ marginLeft: '10px', fontStyle: 'italic', color: isValidDuration(item.duration) ? 'inherit' : 'red' }}>
                {friendlyDuration(item.duration)}
              </span>
              <button type="button" onClick={() => removeItineraryEntry(index)}>Remove</button>
              {index > 0 && (
                <button type="button" onClick={() => moveItineraryEntry(index, index - 1)}>Move Up</button>
              )}
              {index < schedule.itinerary.length - 1 && (
                <button type="button" onClick={() => moveItineraryEntry(index, index + 1)}>Move Down</button>
              )}
            </div>
          ))}
          <button type="button" onClick={addItineraryEntry}>Add Itinerary Entry</button>
        </div>
        <button type="submit">Save</button>
        <button type="button" onClick={() => navigate('/schedules')}>
          Cancel
        </button>
      </form>
    </div>
  );
}
