import fastify from 'fastify';
import mercurius from 'mercurius';

// import loadSchema from './schema';
import { getSchema } from './schema';

interface IGraphQLQuerystring {
  query: string;
}

interface IGraphQLHeaders {
}

export function listen() {
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

  app.listen(process.env.HTTP_PORT as string, '0.0.0.0');
}
