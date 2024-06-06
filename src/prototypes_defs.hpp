#include "Particle.h"


particle::Future<bool> publish(const char* name, PublishFlags flags1, PublishFlags flags2 = PublishFlags());
particle::Future<bool> publish(const char* name, const char* data, PublishFlags flags1, PublishFlags flags2 = PublishFlags());
