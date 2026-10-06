import path from 'path';

import fastify from 'fastify';
import mercurius from 'mercurius';
import staticContent from '@fastify/static';

import { getSchema } from './schema';

export async function listen() {
  const app = fastify({
    logger: true,
  });

  app.register(mercurius, {
    ...getSchema(),
    graphiql: true,
  });

  app.register(staticContent, { root: path.resolve(__dirname, '../public') });

  // Serve index.html for all unmatched routes
  app.setNotFoundHandler((request, reply) => {
    reply.sendFile('index.html');
  });

  return app.listen({
    port: parseInt(process.env.HTTP_PORT as string, 10),
    host: '0.0.0.0',
  });
}
