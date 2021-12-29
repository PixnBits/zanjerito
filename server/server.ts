import path from 'path';

import fastify from 'fastify';
import mercurius from 'mercurius';
import staticContent from 'fastify-static';

import { getSchema } from './schema';

export async function listen() {
  const app = fastify({
    logger: true,
  });

  app.register(mercurius, {
    ...getSchema(),
    graphiql: true,
  });

  app.register(staticContent, { root: path.resolve(__dirname, '../public')})

  return app.listen(process.env.HTTP_PORT as string, '0.0.0.0');
}
