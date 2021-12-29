import * as React from 'react';
import * as ReactDOM from 'react-dom';
import { Provider as URQLProvider } from 'urql';

import { client } from './urql'
import Stations from './stations'

function App() {
  return (
    <>
      <h1>Zanjerito!</h1>
      <p>Running interactively.</p>
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
