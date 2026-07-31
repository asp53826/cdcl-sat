#ifndef SAT_DIMACS_H
#define SAT_DIMACS_H

#include <string>
#include <vector>

#include "sat/solver.h"

namespace sat {

struct ParseResult {
  int vars = 0;
  size_t clauses = 0;
  bool trivially_unsat = false;
  std::string error;
  bool ok() const { return error.empty(); }
};

ParseResult read_dimacs(const std::string& path, Solver& solver);

// Re-reads the file and checks the model against every clause. Deliberately
// independent of the solver's own data structures: checking the model against
// the clause database the solver built would pass even if the parser dropped a
// clause on the floor.
bool verify_model(const std::string& path, const std::vector<int>& model,
                  std::string& why);

}  // namespace sat

#endif
