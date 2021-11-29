interface TypeResolvers {
  [fieldName: string]: (obj: any, args: any, context: any, info: any) => any;
}

interface SchemaResolvers {
  [typeName: string]: TypeResolvers;
}

const schemas: string[] = [
  'type Query',
];
const resolvers: SchemaResolvers = {};

export function addToSchema(schema: string, resolversA: SchemaResolvers) {
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
  };
}
