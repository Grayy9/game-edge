#pragma once

#include <cstdlib>
#include <iostream>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::cerr << __FILE__ << ':' << __LINE__ << ": " #expression " failed\n"; \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)
