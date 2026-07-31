// Unit and differential tests. No framework: a counter, a macro, and a
// non-zero exit code, which is all this needs and keeps the repo dependency
// free.
//
// The interesting test is the last one. Every CDCL component is a heuristic
// that is allowed to change the search but not the answer, so the strongest
// cheap check is that all of them agree on thousands of random instances - and
// that the ones claiming SAT produce a model that satisfies every clause.

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "sat/dimacs.h"
#include "sat/solver.h"

namespace {

int checks = 0;
int failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    checks++;                                                              \
    if (!(cond)) {                                                         \
      failures++;                                                          \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                      \
  } while (0)

#define CHECK_EQ(a, b)                                                     \
  do {                                                                     \
    checks++;                                                              \
    if (!((a) == (b))) {                                                   \
      failures++;                                                          \
      std::printf("FAIL %s:%d  %s != %s\n", __FILE__, __LINE__, #a, #b);   \
    }                                                                      \
  } while (0)

using namespace sat;

Lit pos(Var v) { return mk_lit(v, false); }
Lit nlit(Var v) { return mk_lit(v, true); }

// A clause set as DIMACS-style signed integers, for readability in tests.
Status solve_dimacs(const std::vector<std::vector<int>>& clauses, int n_vars,
                    std::vector<int>* model = nullptr,
                    Options opts = Options{}) {
  Solver s(opts);
  s.reserve_vars(n_vars);
  for (const auto& c : clauses) {
    std::vector<Lit> lits;
    for (int l : c) lits.push_back(mk_lit(std::abs(l) - 1, l < 0));
    s.add_clause(lits);
  }
  const Status st = s.solve();
  if (model && st == Status::Sat) *model = s.model();
  return st;
}

bool model_satisfies(const std::vector<std::vector<int>>& clauses,
                     const std::vector<int>& model) {
  for (const auto& c : clauses) {
    bool ok = false;
    for (int l : c) {
      const size_t idx = static_cast<size_t>(std::abs(l)) - 1;
      if (idx < model.size() && model[idx] == l) { ok = true; break; }
    }
    if (!ok) return false;
  }
  return true;
}

// ---- literal encoding -----------------------------------------------------

void test_literal_encoding() {
  CHECK_EQ(var_of(pos(7)), 7);
  CHECK_EQ(var_of(nlit(7)), 7);
  CHECK(!sign_of(pos(7)));
  CHECK(sign_of(nlit(7)));
  CHECK_EQ(neg(pos(7)), nlit(7));
  CHECK_EQ(neg(nlit(7)), pos(7));
  CHECK_EQ(neg(neg(pos(3))), pos(3));
}

// ---- tiny formulas whose answer is obvious --------------------------------

void test_trivial_cases() {
  CHECK_EQ(solve_dimacs({}, 0), Status::Sat);
  CHECK_EQ(solve_dimacs({}, 5), Status::Sat);
  CHECK_EQ(solve_dimacs({{1}}, 1), Status::Sat);
  CHECK_EQ(solve_dimacs({{1}, {-1}}, 1), Status::Unsat);
  CHECK_EQ(solve_dimacs({{1, 2}, {-1}, {-2}}, 2), Status::Unsat);
  CHECK_EQ(solve_dimacs({{1, 2}, {-1, 2}, {1, -2}, {-1, -2}}, 2), Status::Unsat);
  CHECK_EQ(solve_dimacs({{1, 2}, {-1, 2}, {1, -2}}, 2), Status::Sat);
}

void test_tautology_and_duplicates() {
  // p or not p is always true and must not make the formula unsatisfiable
  CHECK_EQ(solve_dimacs({{1, -1}, {2}}, 2), Status::Sat);
  // a duplicated literal is not a longer clause
  CHECK_EQ(solve_dimacs({{1, 1}, {-1}}, 1), Status::Unsat);
}

void test_unit_propagation_chain() {
  // 1, and 1 -> 2, and 2 -> 3, and not 3
  CHECK_EQ(solve_dimacs({{1}, {-1, 2}, {-2, 3}, {-3}}, 3), Status::Unsat);
  CHECK_EQ(solve_dimacs({{1}, {-1, 2}, {-2, 3}, {3}}, 3), Status::Sat);
}

void test_pigeonhole_is_unsat() {
  // 3 pigeons, 2 holes
  std::vector<std::vector<int>> clauses;
  auto x = [](int p, int h) { return p * 2 + h + 1; };
  for (int p = 0; p < 3; p++) clauses.push_back({x(p, 0), x(p, 1)});
  for (int h = 0; h < 2; h++)
    for (int p = 0; p < 3; p++)
      for (int q = p + 1; q < 3; q++)
        clauses.push_back({-x(p, h), -x(q, h)});
  CHECK_EQ(solve_dimacs(clauses, 6), Status::Unsat);
}

void test_model_is_returned_and_correct() {
  const std::vector<std::vector<int>> f = {{1, 2, 3}, {-1, -2}, {-2, -3}, {2}};
  std::vector<int> model;
  CHECK_EQ(solve_dimacs(f, 3, &model), Status::Sat);
  CHECK_EQ(model.size(), 3u);
  CHECK(model_satisfies(f, model));
}

// ---- clause database ------------------------------------------------------

void test_root_level_conflict_is_caught_at_add_time() {
  Solver s;
  s.reserve_vars(2);
  CHECK(s.add_clause({pos(0)}));
  CHECK(!s.add_clause({nlit(0)}));
  CHECK_EQ(s.solve(), Status::Unsat);
}

void test_stats_move() {
  std::vector<std::vector<int>> clauses;
  auto x = [](int p, int h) { return p * 5 + h + 1; };
  for (int p = 0; p < 6; p++) {
    std::vector<int> c;
    for (int h = 0; h < 5; h++) c.push_back(x(p, h));
    clauses.push_back(c);
  }
  for (int h = 0; h < 5; h++)
    for (int p = 0; p < 6; p++)
      for (int q = p + 1; q < 6; q++)
        clauses.push_back({-x(p, h), -x(q, h)});

  Solver s;
  s.reserve_vars(30);
  for (const auto& c : clauses) {
    std::vector<Lit> lits;
    for (int l : c) lits.push_back(mk_lit(std::abs(l) - 1, l < 0));
    s.add_clause(lits);
  }
  CHECK_EQ(s.solve(), Status::Unsat);
  CHECK(s.stats().conflicts > 0);
  CHECK(s.stats().decisions > 0);
  CHECK(s.stats().propagations > 0);
  CHECK(s.stats().learned > 0);
}

void test_conflict_budget_gives_up_rather_than_lying() {
  std::vector<std::vector<int>> clauses;
  auto x = [](int p, int h) { return p * 9 + h + 1; };
  for (int p = 0; p < 10; p++) {
    std::vector<int> c;
    for (int h = 0; h < 9; h++) c.push_back(x(p, h));
    clauses.push_back(c);
  }
  for (int h = 0; h < 9; h++)
    for (int p = 0; p < 10; p++)
      for (int q = p + 1; q < 10; q++)
        clauses.push_back({-x(p, h), -x(q, h)});

  Options opts;
  opts.conflict_budget = 50;
  CHECK_EQ(solve_dimacs(clauses, 90, nullptr, opts), Status::Unknown);
}

// ---- differential ---------------------------------------------------------

std::vector<std::vector<int>> random_3sat(std::mt19937& rng, int n, double ratio) {
  const int m = static_cast<int>(n * ratio);
  std::uniform_int_distribution<int> pick(1, n);
  std::uniform_int_distribution<int> coin(0, 1);
  std::vector<std::vector<int>> out;
  for (int i = 0; i < m; i++) {
    std::vector<int> c;
    while (c.size() < 3) {
      const int v = pick(rng);
      bool dup = false;
      for (int l : c) if (std::abs(l) == v) dup = true;
      if (!dup) c.push_back(coin(rng) ? v : -v);
    }
    out.push_back(c);
  }
  return out;
}

void test_all_configurations_agree() {
  std::mt19937 rng(20260731);
  Options full;
  Options no_min = full;    no_min.minimize = false;
  Options no_phase = full;  no_phase.phase_saving = false;
  Options fast = full;      fast.restart_base = 10;
  Options slow = full;      slow.restart_base = 100000;

  int sat = 0, unsat = 0;
  for (int i = 0; i < 400; i++) {
    const int n = 20 + (i % 25);
    const double ratio = 3.4 + 0.005 * (i % 400);
    const auto f = random_3sat(rng, n, ratio);

    std::vector<int> model;
    const Status a = solve_dimacs(f, n, &model, full);
    if (a == Status::Sat) {
      sat++;
      // The model check is the half of the oracle that does not need another
      // solver, and it is the half that catches a wrong answer rather than a
      // merely different one.
      CHECK(model_satisfies(f, model));
    } else {
      unsat++;
    }

    for (const Options& o : {no_min, no_phase, fast, slow}) {
      CHECK_EQ(solve_dimacs(f, n, nullptr, o), a);
    }
  }
  // If the ratio sweep produced only one answer the test proved nothing.
  CHECK(sat > 40);
  CHECK(unsat > 40);
}

void test_adding_a_learned_consequence_changes_nothing() {
  // A satisfiable formula stays satisfiable when a clause implied by it is
  // added, and the model still checks out.
  std::mt19937 rng(7);
  for (int i = 0; i < 50; i++) {
    const int n = 30;
    auto f = random_3sat(rng, n, 3.0);
    std::vector<int> model;
    if (solve_dimacs(f, n, &model) != Status::Sat) continue;
    // pick a clause already satisfied by the model and add it again
    f.push_back({model[0], model[1]});
    std::vector<int> model2;
    CHECK_EQ(solve_dimacs(f, n, &model2), Status::Sat);
    CHECK(model_satisfies(f, model2));
  }
}

}  // namespace

int main() {
  test_literal_encoding();
  test_trivial_cases();
  test_tautology_and_duplicates();
  test_unit_propagation_chain();
  test_pigeonhole_is_unsat();
  test_model_is_returned_and_correct();
  test_root_level_conflict_is_caught_at_add_time();
  test_stats_move();
  test_conflict_budget_gives_up_rather_than_lying();
  test_all_configurations_agree();
  test_adding_a_learned_consequence_changes_nothing();

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
