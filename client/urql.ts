import { createClient, defaultExchanges, subscriptionExchange } from 'urql';
import { createClient as createWSClient } from 'graphql-ws';


const wsClient = createWSClient({
  url: (function(){
    const u = new URL(window.location.href);
    u.protocol = 'ws:';
    u.pathname = '/graphql';
    return u.toString();
  }()),
});

export const client = createClient({
  url: '/graphql',
  exchanges: [
    ...defaultExchanges,
    subscriptionExchange({
      forwardSubscription: (operation) => ({
        subscribe: (sink) => ({
          unsubscribe: wsClient.subscribe(operation, sink),
        }),
      }),
    })
  ]
});
