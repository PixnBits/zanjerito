import * as React from 'react';
import { useParams, useNavigate } from 'react-router';
import { useQuery, useMutation } from 'urql';

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

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
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
            </div>
          ))}
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
            </div>
          ))}
        </div>
        <button type="submit">Save</button>
        <button type="button" onClick={() => navigate('/schedules')}>
          Cancel
        </button>
      </form>
    </div>
  );
}
