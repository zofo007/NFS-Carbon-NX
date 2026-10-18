#include <limits.h>

_Static_assert(PATH_MAX >= 0x301, "Path limit must fit libnx FS_MAX_PATH.");
_Static_assert(NAME_MAX >= 255, "Newlib filename limit is missing.");
_Static_assert(CHAR_BIT == 8, "Expected eight-bit bytes.");

int carbon_limits_probe(void) { return PATH_MAX; }
