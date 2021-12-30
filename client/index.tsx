import * as React from 'react';
import * as ReactDOM from 'react-dom';
import { Provider as URQLProvider } from 'urql';

import { client, subscriptionClient } from './urql'
import Stations from './Stations'
import ConnectionStatus from './ConnectionStatus'

function App() {
  return (
    <>
      <h1>Zanjerito!</h1>
      <ConnectionStatus client={subscriptionClient} />
      <Stations />
    </>
  )
}

ReactDOM.render(
  <React.StrictMode>
    <URQLProvider value={client}>
      <App />
    </URQLProvider>
  </React.StrictMode>,
  document.getElementById('root')
);
