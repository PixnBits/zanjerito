import * as React from 'react';
import { useParams, useNavigate } from 'react-router';
import { useQuery, useMutation } from 'urql';

const stationQuery = `
  query ($id: ID!) {
    station(id: $id) {
      id
      title
      notes
    }
  }
`;

const saveStationMutation = `
  mutation ($id: ID, $title: String!, $notes: String) {
    saveStation(id: $id, title: $title, notes: $notes) {
      id
      title
      notes
    }
  }
`;

export default function EditStation() {
  const { id } = useParams();
  const navigate = useNavigate();
  const [station, setStation] = React.useState({ title: '', notes: '' });

  const [{ data, fetching, error }] = useQuery({
    query: stationQuery,
    variables: { id },
    pause: !id, // Skip query if creating a new station
  });

  const [, saveStation] = useMutation(saveStationMutation);

  React.useEffect(() => {
    if (data?.station) {
      setStation(data.station);
    }
  }, [data]);

  const handleChange = (e: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>) => {
    const { name, value } = e.target;
    setStation((prev) => ({ ...prev, [name]: value }));
  };

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    await saveStation({ id, ...station });
    navigate('/stations');
  };

  if (fetching) return <p>Loading...</p>;
  if (error) return <p>Error: {error.message}</p>;

  return (
    <div>
      <h2>{id ? 'Edit Station' : 'Add New Station'}</h2>
      <form onSubmit={handleSubmit}>
        <div>
          <label>
            Title:
            <input
              type="text"
              name="title"
              value={station.title}
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
              value={station.notes}
              onChange={handleChange}
            />
          </label>
        </div>
        <button type="submit">Save</button>
        <button type="button" onClick={() => navigate('/stations')}>
          Cancel
        </button>
      </form>
    </div>
  );
}
