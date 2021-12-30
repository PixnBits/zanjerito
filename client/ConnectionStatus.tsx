import { Client } from 'graphql-ws'

import * as React from 'react'
import * as PropTypes from 'prop-types'

export default function ConnectionStatus({ client }: { client: Client }) {
  const [connectionState, setConnectionState] = React.useState(null);
  React.useEffect(() => {
    function setConnected() {
      setConnectionState('connected');
    }
    client.on('connected', setConnected);

    function setClosed() {
      setConnectionState('disconnected');
    }
    client.on('closed', setClosed);

    return function cleanup() {
      // can only add, can't remove (yet)
      // client.removeEventListener('connected', setConnected);
      // client.removeEventListener('closed', setClosed);
    }
  });

  // TODO: use portals? to render a banner at the top when disconnected
  return <p>Real-time data status: {connectionState || 'unknown'}</p>;
}

ConnectionStatus.propTypes = {
  client: PropTypes.shape({
    on: PropTypes.func.isRequired,
  }),
};
