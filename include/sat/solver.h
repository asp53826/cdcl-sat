// CDCL SAT solver: watched literals, 1UIP learning, VSIDS, Luby restarts,
// clause database reduction, and DRAT proof emission.
//
// Literal encoding: variable v (0-based) becomes 2v for positive and 2v+1 for
// negative, so negation is a xor and the watch lists index directly by literal.
// DIMACS is 1-based and signed; the conversion lives in dimacs.cpp and nowhere
// else.

#ifndef SAT_SOLVER_H
#define SAT_SOLVER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sat {

using Var = int32_t;
using Lit = int32_t;

constexpr Lit LIT_UNDEF = -1;
constexpr int32_t CREF_UNDEF = -1;

inline Lit mk_lit(Var v, bool negated) { return (v << 1) | (negated ? 1 : 0); }
inline Lit neg(Lit l) { return l ^ 1; }
inline Var var_of(Lit l) { return l >> 1; }
inline bool sign_of(Lit l) { return (l & 1) != 0; }

enum class Value : uint8_t { False = 0, True = 1, Undef = 2 };

inline Value negate(Value v) {
  if (v == Value::Undef) return Value::Undef;
  return v == Value::True ? Value::False : Value::True;
}

enum class Status { Sat, Unsat, Unknown };

struct Stats {
  uint64_t decisions = 0;
  uint64_t propagations = 0;
  uint64_t conflicts = 0;
  uint64_t restarts = 0;
  uint64_t learned = 0;
  uint64_t learned_lits = 0;
  uint64_t minimized_lits = 0;
  uint64_t reductions = 0;
  uint64_t deleted = 0;
  uint64_t proof_lines = 0;
  double seconds = 0.0;
};

struct Options {
  double var_decay = 0.95;
  double clause_decay = 0.999;
  int restart_base = 100;      // Luby unit, in conflicts
  double reduce_fraction = 0.5;
  int reduce_first = 2000;     // learned clauses before the first reduction
  int reduce_step = 300;       // growth of the limit after each reduction
  bool minimize = true;        // recursive conflict-clause minimization
  bool phase_saving = true;
  uint64_t conflict_budget = 0;  // 0 means unlimited
};

// Clauses live in one contiguous arena. A CRef is a word offset into it, which
// keeps the watch lists at 8 bytes per entry and survives arena growth in a way
// that raw pointers do not.
class ClauseArena {
 public:
  struct Header {
    uint32_t size;
    uint32_t learned : 1;
    uint32_t deleted : 1;
    uint32_t lbd : 30;
    float activity;
  };

  int32_t alloc(const std::vector<Lit>& lits, bool learned, uint32_t lbd);
  Header& header(int32_t ref) { return *reinterpret_cast<Header*>(&data_[ref]); }
  const Header& header(int32_t ref) const {
    return *reinterpret_cast<const Header*>(&data_[ref]);
  }
  Lit* lits(int32_t ref) { return reinterpret_cast<Lit*>(&data_[ref + kHeaderWords]); }
  const Lit* lits(int32_t ref) const {
    return reinterpret_cast<const Lit*>(&data_[ref + kHeaderWords]);
  }
  uint32_t size(int32_t ref) const { return header(ref).size; }
  size_t words() const { return data_.size(); }
  size_t wasted() const { return wasted_; }
  void mark_free(int32_t ref);
  void clear() { data_.clear(); wasted_ = 0; }

 private:
  static constexpr size_t kHeaderWords = sizeof(Header) / sizeof(uint32_t);
  std::vector<uint32_t> data_;
  size_t wasted_ = 0;
};

class Proof;

class Solver {
 public:
  explicit Solver(Options opts = Options{});
  ~Solver();

  Var new_var();
  void reserve_vars(int n);
  // Returns false if the formula is already unsatisfiable at level 0.
  bool add_clause(std::vector<Lit> lits);

  Status solve();

  int num_vars() const { return static_cast<int>(assign_.size()); }
  size_t num_clauses() const { return clauses_.size(); }
  const Stats& stats() const { return stats_; }
  Value value(Var v) const { return assign_[v]; }
  // Model as DIMACS-signed integers, valid only after Status::Sat.
  std::vector<int> model() const;

  void set_proof(Proof* p) { proof_ = p; }

 private:
  struct Watcher {
    int32_t cref;
    Lit blocker;  // if this literal is true the clause is satisfied and the
                  // arena never has to be touched
  };

  Value lit_value(Lit l) const {
    Value v = assign_[var_of(l)];
    return sign_of(l) ? negate(v) : v;
  }

  void enqueue(Lit l, int32_t reason);
  int32_t propagate();
  void analyze(int32_t conflict, std::vector<Lit>& learnt, int& backtrack_level);
  bool redundant(Lit l, uint32_t abstract_levels);
  void backtrack(int level);
  Lit pick_branch();
  void bump_var(Var v);
  void decay_vars();
  void bump_clause(int32_t ref);
  void attach(int32_t ref);
  void detach(int32_t ref);
  void reduce_db();
  uint32_t compute_lbd(const std::vector<Lit>& lits);
  bool simplify_root();
  static double luby(double y, int x);

  Options opts_;
  Stats stats_;
  Proof* proof_ = nullptr;
  bool unsat_ = false;

  ClauseArena arena_;
  std::vector<int32_t> clauses_;
  std::vector<int32_t> learnts_;
  std::vector<std::vector<Watcher>> watches_;  // indexed by literal

  std::vector<Value> assign_;
  std::vector<int32_t> reason_;
  std::vector<int32_t> level_;
  std::vector<Lit> trail_;
  std::vector<int32_t> trail_lim_;
  size_t qhead_ = 0;

  std::vector<double> activity_;
  double var_inc_ = 1.0;
  double clause_inc_ = 1.0;
  std::vector<int32_t> heap_;      // binary heap of vars ordered by activity
  std::vector<int32_t> heap_pos_;  // var -> index in heap_, -1 if absent
  std::vector<uint8_t> saved_phase_;

  std::vector<uint8_t> seen_;
  std::vector<Lit> analyze_stack_;
  std::vector<Lit> analyze_clear_;
  std::vector<uint64_t> lbd_stamp_;
  uint64_t lbd_counter_ = 0;

  void heap_insert(Var v);
  void heap_up(int i);
  void heap_down(int i);
  Var heap_pop();
  bool heap_empty() const { return heap_.empty(); }
  bool in_heap(Var v) const { return heap_pos_[v] >= 0; }
};

}  // namespace sat

#endif
