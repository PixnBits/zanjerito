import { createClient, defaultExchanges, subscriptionExchange } from 'urql';
import { createClient as createWSClient } from 'graphql-ws';

export const subscriptionClient = createWSClient({
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
          unsubscribe: subscriptionClient.subscribe(operation, sink),
        }),
      }),
    })
  ]
});
