import { IResolverObject, MercuriusContext } from 'mercurius'

import MQEmitter from 'mqemitter';

interface Resolvers {
  [key: string]: IResolverObject<any, MercuriusContext>
}

const schemas: string[] = [
  'type Query',
  // 'type Mutation',
  'type Subscription',
];

const resolvers: Resolvers = {};
// TS7009 requires `class` syntax instead of function constructor comprehension?
/* @ts-ignore 7009 */
export const subscriptionEmitter = new MQEmitter();

export function addToSchema(schema: string, resolversA: Resolvers) {
  schemas.push(schema);
  for (const [typeName, typeResolvers] of Object.entries(resolversA)) {
    if (!resolvers[typeName]) {
      resolvers[typeName] = {};
    }
    const storedTypeResolvers = resolvers[typeName];
    for (const [fieldName, fieldResolver] of Object.entries(typeResolvers)) {
      if (storedTypeResolvers[fieldName]) {
        throw new Error(`unable to add duplicate resolver for ${typeName}.${fieldName}, already added previously`);
      }
      storedTypeResolvers[fieldName] = fieldResolver;
    }
  }
}

export function getSchema() {
  return {
    schema: schemas.join('\n\n'),
    resolvers,
    subscription: {
      emitter: subscriptionEmitter,
    },
  };
}
