import fastify from 'fastify';
import mercurius from 'mercurius';

import { getSchema } from './schema';

export async function listen() {
  const app = fastify({
    logger: true,
  });

  app.register(mercurius, {
    ...getSchema(),
    graphiql: true,
  });

  app.get('/', async function (request, reply) {
    reply
      .code(200)
      .send({ hello: 'world' })
  });

  return app.listen(process.env.HTTP_PORT as string, '0.0.0.0');
}
