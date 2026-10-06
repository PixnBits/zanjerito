import * as React from 'react';
import { createRoot } from 'react-dom/client';
import { BrowserRouter, Routes, Route, Link } from 'react-router';
import { Provider as URQLProvider } from 'urql';

import { client, subscriptionClient } from './urql';
import ConnectionStatus from './ConnectionStatus';
import Home from './pages/Home';
import Stations from './pages/Stations';
import Schedules from './pages/Schedules';
import EditStation from './pages/EditStation';
import EditSchedule from './pages/EditSchedule';

function App() {
  return (
    <BrowserRouter>
      <header>
        <h1>Zanjerito</h1>
        <ConnectionStatus client={subscriptionClient} />
        <nav>
          <Link to="/">Home</Link>
          <Link to="/stations">Stations</Link>
          <Link to="/schedules">Schedules</Link>
        </nav>
      </header>
      <main>
        <Routes>
          <Route path="/" element={<Home />} />
          <Route path="/stations" element={<Stations />} />
          <Route path="/stations/edit/:id" element={<EditStation />} />
          <Route path="/schedules" element={<Schedules />} />
          <Route path="/schedules/edit/:id" element={<EditSchedule />} />
        </Routes>
      </main>
    </BrowserRouter>
  );
}

const container = document.getElementById('root');
const root = createRoot(container!);

root.render(
  <React.StrictMode>
    <URQLProvider value={client}>
      <App />
    </URQLProvider>
  </React.StrictMode>
);
