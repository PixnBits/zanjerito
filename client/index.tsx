import * as React from 'react';
import * as ReactDOM from 'react-dom';

function App() {
  return (
    <>
      <h1>Zanjerito!</h1>
      <p>Running interactively.</p>
    </>
  )
}

ReactDOM.render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
  document.getElementById('root')
)
