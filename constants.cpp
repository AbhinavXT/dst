#include "Constants.h"

// Out-of-line definition for the lone static member of ConstantsDef. The
// original code defined this in datastore.cpp, which was a junk-drawer
// placement — it has nothing to do with the DataStore class. Putting it
// next to its declaration makes the dependency graph readable.
//
// xalpha is a horizontal scaling factor the Structures.h geometry helpers
// use (start_abs_pos * xalpha → screen X). DLConsole itself does not draw
// any of that geometry, so the value (1.0) is effectively unused, but we
// keep the symbol present so any code paths from sibling projects that
// share these headers still link.
double ConstantsDef::xalpha = 1.0;
