// DRAT proof emission.
//
// The format is deliberately dumb: a line of DIMACS-signed literals terminated
// by 0 is a clause addition, the same prefixed by "d " is a deletion, and a
// bare "0" is the empty clause. That is the whole specification, which is why
// an independent checker can be a single C file and why "my solver says UNSAT"
// can be upgraded to "drat-trim agrees".
//
// Every learned clause is emitted at the moment it is learned, before it can be
// used to derive anything else. Emitting late would produce a proof whose steps
// do not follow from the steps above them, which the checker will reject - and
// rightly, because the solver would then be claiming a derivation it did not
// perform in that order.

#ifndef SAT_PROOF_H
#define SAT_PROOF_H

#include <cstdio>
#include <string>
#include <vector>

#include "sat/solver.h"

namespace sat {

class Proof {
 public:
  explicit Proof(const std::string& path);
  ~Proof();

  bool ok() const { return file_ != nullptr; }
  uint64_t lines() const { return lines_; }

  void add(const std::vector<Lit>& lits);
  void del(const Lit* lits, uint32_t n);
  void empty_clause();
  void close();

 private:
  void write_lits(const Lit* lits, uint32_t n);

  std::FILE* file_ = nullptr;
  uint64_t lines_ = 0;
  std::string buf_;
};

}  // namespace sat

#endif
