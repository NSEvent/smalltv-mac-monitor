#pragma once
#include "app.h"
#include <IPAddress.h>

bool fetchReading(const char *url, Reading *reading,
                  bool (*resolve)(const char *, IPAddress *));
