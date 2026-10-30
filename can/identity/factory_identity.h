#ifndef FACTORY_IDENTITY_H
#define FACTORY_IDENTITY_H

#include "shared/factory_identity.h"

/** Read the factory-programmed identity. False means unreadable or erased. */
bool FactoryIdentity_Read(factory_identity_t *identity);

#endif /* FACTORY_IDENTITY_H */
