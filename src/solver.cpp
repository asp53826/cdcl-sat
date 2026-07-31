#include "sat/solver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "sat/proof.h"

namespace sat {

// ---- clause arena ---------------------------------------------------------

int32_t ClauseArena::alloc(const std::vector<Lit>& lits, bool learned,
                           uint32_t lbd) {
  const int32_t ref = static_cast<int32_t>(data_.size());
  data_.resize(data_.size() + kHeaderWords + lits.size());
  Header& h = header(ref);
  h.size = static_cast<uint32_t>(lits.size());
  h.learned = learned ? 1u : 0u;
  h.deleted = 0;
  h.lbd = lbd;
  h.activity = 0.0f;
  Lit* dst = this->lits(ref);
  std::memcpy(dst, lits.data(), lits.size() * sizeof(Lit));
  return ref;
}

void ClauseArena::mark_free(int32_t ref) {
  Header& h = header(ref);
  if (h.deleted) return;
  h.deleted = 1;
  wasted_ += kHeaderWords + h.size;
}

// ---- construction ---------------------------------------------------------

Solver::Solver(Options opts) : opts_(opts) {}
Solver::~Solver() = default;

Var Solver::new_var() {
  const Var v = static_cast<Var>(assign_.size());
  assign_.push_back(Value::Undef);
  reason_.push_back(CREF_UNDEF);
  level_.push_back(0);
  activity_.push_back(0.0);
  heap_pos_.push_back(-1);
  saved_phase_.push_back(1);  // false-first, which is MiniSat's default and
                              // measurably better than true-first on CNF that
                              // came out of an encoder
  seen_.push_back(0);
  lbd_stamp_.push_back(0);
  watches_.emplace_back();
  watches_.emplace_back();
  heap_insert(v);
  return v;
}

void Solver::reserve_vars(int n) {
  while (num_vars() < n) new_var();
}

// ---- activity heap --------------------------------------------------------

void Solver::heap_up(int i) {
  const int32_t v = heap_[i];
  while (i > 0) {
    const int parent = (i - 1) >> 1;
    if (activity_[heap_[parent]] >= activity_[v]) break;
    heap_[i] = heap_[parent];
    heap_pos_[heap_[i]] = i;
    i = parent;
  }
  heap_[i] = v;
  heap_pos_[v] = i;
}

void Solver::heap_down(int i) {
  const int32_t v = heap_[i];
  const int n = static_cast<int>(heap_.size());
  while (true) {
    int child = 2 * i + 1;
    if (child >= n) break;
    if (child + 1 < n && activity_[heap_[child + 1]] > activity_[heap_[child]]) {
      child++;
    }
    if (activity_[heap_[child]] <= activity_[v]) break;
    heap_[i] = heap_[child];
    heap_pos_[heap_[i]] = i;
    i = child;
  }
  heap_[i] = v;
  heap_pos_[v] = i;
}

void Solver::heap_insert(Var v) {
  if (heap_pos_[v] >= 0) return;
  heap_.push_back(v);
  heap_pos_[v] = static_cast<int32_t>(heap_.size()) - 1;
  heap_up(static_cast<int>(heap_.size()) - 1);
}

Var Solver::heap_pop() {
  const Var top = heap_[0];
  heap_pos_[top] = -1;
  heap_[0] = heap_.back();
  heap_.pop_back();
  if (!heap_.empty()) {
    heap_pos_[heap_[0]] = 0;
    heap_down(0);
  }
  return top;
}

void Solver::bump_var(Var v) {
  activity_[v] += var_inc_;
  if (activity_[v] > 1e100) {
    // Rescale rather than let the doubles denormalise. The ordering is what
    // matters and it is preserved exactly.
    for (double& a : activity_) a *= 1e-100;
    var_inc_ *= 1e-100;
  }
  if (in_heap(v)) heap_up(heap_pos_[v]);
}

void Solver::decay_vars() { var_inc_ /= opts_.var_decay; }

void Solver::bump_clause(int32_t ref) {
  ClauseArena::Header& h = arena_.header(ref);
  h.activity += static_cast<float>(clause_inc_);
  if (h.activity > 1e20f) {
    for (int32_t r : learnts_) arena_.header(r).activity *= 1e-20f;
    clause_inc_ *= 1e-20;
  }
}

// ---- clause database ------------------------------------------------------

void Solver::attach(int32_t ref) {
  const Lit* ls = arena_.lits(ref);
  watches_[neg(ls[0])].push_back({ref, ls[1]});
  watches_[neg(ls[1])].push_back({ref, ls[0]});
}

void Solver::detach(int32_t ref) {
  const Lit* ls = arena_.lits(ref);
  for (Lit w : {neg(ls[0]), neg(ls[1])}) {
    auto& ws = watches_[w];
    for (size_t i = 0; i < ws.size(); i++) {
      if (ws[i].cref == ref) {
        ws[i] = ws.back();
        ws.pop_back();
        break;
      }
    }
  }
}

bool Solver::add_clause(std::vector<Lit> lits) {
  if (unsat_) return false;

  std::sort(lits.begin(), lits.end());
  std::vector<Lit> out;
  out.reserve(lits.size());
  Lit prev = LIT_UNDEF;
  for (Lit l : lits) {
    if (l == prev) continue;             // duplicate literal
    if (prev != LIT_UNDEF && l == neg(prev)) return true;  // tautology
    if (lit_value(l) == Value::True) return true;          // already satisfied
    if (lit_value(l) == Value::False) { prev = l; continue; }
    out.push_back(l);
    prev = l;
  }

  if (out.empty()) {
    unsat_ = true;
    if (proof_) proof_->empty_clause();
    return false;
  }
  if (out.size() == 1) {
    enqueue(out[0], CREF_UNDEF);
    if (propagate() != CREF_UNDEF) {
      unsat_ = true;
      if (proof_) proof_->empty_clause();
      return false;
    }
    return true;
  }

  const int32_t ref = arena_.alloc(out, false, 0);
  clauses_.push_back(ref);
  attach(ref);
  return true;
}

// ---- assignment -----------------------------------------------------------

void Solver::enqueue(Lit l, int32_t reason) {
  const Var v = var_of(l);
  assign_[v] = sign_of(l) ? Value::False : Value::True;
  reason_[v] = reason;
  level_[v] = static_cast<int32_t>(trail_lim_.size());
  trail_.push_back(l);
}

int32_t Solver::propagate() {
  int32_t conflict = CREF_UNDEF;

  while (qhead_ < trail_.size()) {
    const Lit p = trail_[qhead_++];
    stats_.propagations++;
    auto& ws = watches_[p];

    size_t i = 0, j = 0;
    while (i < ws.size()) {
      const Watcher w = ws[i];

      // The blocker is a second literal from the clause cached in the watch
      // list. If it is already true the clause cannot propagate and the arena
      // is never touched, which is most of the win in this loop.
      if (lit_value(w.blocker) == Value::True) {
        ws[j++] = ws[i++];
        continue;
      }
      i++;

      const int32_t ref = w.cref;
      Lit* ls = arena_.lits(ref);
      const uint32_t size = arena_.size(ref);

      // Normalise so the falsified literal sits at index 1. analyze() relies on
      // the propagated literal being at index 0 in any clause used as a reason.
      if (ls[0] == neg(p)) std::swap(ls[0], ls[1]);

      const Lit first = ls[0];
      if (first != w.blocker && lit_value(first) == Value::True) {
        ws[j++] = {ref, first};
        continue;
      }

      bool moved = false;
      for (uint32_t k = 2; k < size; k++) {
        if (lit_value(ls[k]) != Value::False) {
          std::swap(ls[1], ls[k]);
          watches_[neg(ls[1])].push_back({ref, first});
          moved = true;
          break;
        }
      }
      if (moved) continue;

      ws[j++] = {ref, first};
      if (lit_value(first) == Value::False) {
        conflict = ref;
        qhead_ = trail_.size();
        while (i < ws.size()) ws[j++] = ws[i++];
      } else {
        enqueue(first, ref);
      }
    }
    ws.resize(j);
    if (conflict != CREF_UNDEF) break;
  }
  return conflict;
}

void Solver::backtrack(int level) {
  if (static_cast<int>(trail_lim_.size()) <= level) return;
  for (int i = static_cast<int>(trail_.size()) - 1; i >= trail_lim_[level]; i--) {
    const Var v = var_of(trail_[i]);
    if (opts_.phase_saving) {
      saved_phase_[v] = assign_[v] == Value::True ? 0 : 1;
    }
    assign_[v] = Value::Undef;
    reason_[v] = CREF_UNDEF;
    heap_insert(v);
  }
  trail_.resize(trail_lim_[level]);
  trail_lim_.resize(level);
  qhead_ = trail_.size();
}

// ---- conflict analysis ----------------------------------------------------

namespace {
inline uint32_t abstract_level(int32_t lvl) { return 1u << (lvl & 31); }
}  // namespace

bool Solver::redundant(Lit l, uint32_t abstract_levels) {
  analyze_stack_.clear();
  analyze_stack_.push_back(l);
  const size_t top = analyze_clear_.size();

  while (!analyze_stack_.empty()) {
    const Lit q = analyze_stack_.back();
    analyze_stack_.pop_back();
    const int32_t r = reason_[var_of(q)];
    const Lit* ls = arena_.lits(r);
    const uint32_t size = arena_.size(r);

    for (uint32_t i = 1; i < size; i++) {
      const Lit p = ls[i];
      const Var v = var_of(p);
      if (seen_[v] || level_[v] == 0) continue;

      // A literal with no reason is a decision and cannot be resolved away.
      // The abstract level test is a cheap filter: if the literal's level is
      // not represented in the learnt clause at all, removal cannot succeed.
      if (reason_[v] != CREF_UNDEF &&
          (abstract_level(level_[v]) & abstract_levels) != 0) {
        seen_[v] = 1;
        analyze_stack_.push_back(p);
        analyze_clear_.push_back(p);
      } else {
        for (size_t k = top; k < analyze_clear_.size(); k++) {
          seen_[var_of(analyze_clear_[k])] = 0;
        }
        analyze_clear_.resize(top);
        return false;
      }
    }
  }
  return true;
}

void Solver::analyze(int32_t conflict, std::vector<Lit>& learnt,
                     int& backtrack_level) {
  learnt.clear();
  learnt.push_back(LIT_UNDEF);  // room for the asserting literal

  int path = 0;
  Lit p = LIT_UNDEF;
  int index = static_cast<int>(trail_.size()) - 1;
  const int decision_level = static_cast<int>(trail_lim_.size());

  do {
    const int32_t ref = conflict;
    if (arena_.header(ref).learned) bump_clause(ref);
    const Lit* ls = arena_.lits(ref);
    const uint32_t size = arena_.size(ref);

    for (uint32_t j = (p == LIT_UNDEF) ? 0 : 1; j < size; j++) {
      const Lit q = ls[j];
      const Var v = var_of(q);
      if (seen_[v] || level_[v] == 0) continue;
      bump_var(v);
      seen_[v] = 1;
      if (level_[v] >= decision_level) {
        path++;
      } else {
        learnt.push_back(q);
      }
    }

    while (!seen_[var_of(trail_[index--])]) {}
    p = trail_[index + 1];
    conflict = reason_[var_of(p)];
    seen_[var_of(p)] = 0;
    path--;
  } while (path > 0);

  learnt[0] = neg(p);

  // Conflict clause minimization. Drop any literal that is implied by the
  // others already in the clause, which is worth doing because a shorter clause
  // both propagates more often and costs less to keep.
  analyze_clear_.assign(learnt.begin(), learnt.end());
  if (opts_.minimize) {
    uint32_t levels = 0;
    for (size_t i = 1; i < learnt.size(); i++) {
      levels |= abstract_level(level_[var_of(learnt[i])]);
    }
    size_t j = 1;
    for (size_t i = 1; i < learnt.size(); i++) {
      if (reason_[var_of(learnt[i])] == CREF_UNDEF ||
          !redundant(learnt[i], levels)) {
        learnt[j++] = learnt[i];
      }
    }
    stats_.minimized_lits += learnt.size() - j;
    learnt.resize(j);
  }

  if (learnt.size() == 1) {
    backtrack_level = 0;
  } else {
    size_t max_i = 1;
    for (size_t i = 2; i < learnt.size(); i++) {
      if (level_[var_of(learnt[i])] > level_[var_of(learnt[max_i])]) max_i = i;
    }
    std::swap(learnt[1], learnt[max_i]);
    backtrack_level = level_[var_of(learnt[1])];
  }

  for (Lit l : analyze_clear_) seen_[var_of(l)] = 0;
  analyze_clear_.clear();
}

uint32_t Solver::compute_lbd(const std::vector<Lit>& lits) {
  lbd_counter_++;
  uint32_t lbd = 0;
  for (Lit l : lits) {
    const int32_t lvl = level_[var_of(l)];
    if (lbd_stamp_[lvl] != lbd_counter_) {
      lbd_stamp_[lvl] = lbd_counter_;
      lbd++;
    }
  }
  return lbd;
}

// ---- database reduction ---------------------------------------------------

void Solver::reduce_db() {
  stats_.reductions++;

  std::sort(learnts_.begin(), learnts_.end(), [this](int32_t a, int32_t b) {
    const auto& ha = arena_.header(a);
    const auto& hb = arena_.header(b);
    if (ha.lbd != hb.lbd) return ha.lbd > hb.lbd;
    return ha.activity < hb.activity;
  });

  const size_t target =
      static_cast<size_t>(learnts_.size() * opts_.reduce_fraction);
  size_t removed = 0;
  size_t keep = 0;

  for (size_t i = 0; i < learnts_.size(); i++) {
    const int32_t ref = learnts_[i];
    const auto& h = arena_.header(ref);
    // Never drop a clause that is currently a reason - the trail would lose the
    // justification for an assignment it still holds - and never drop the glue
    // clauses, which is the one heuristic every solver since Glucose agrees on.
    const bool locked = reason_[var_of(arena_.lits(ref)[0])] == ref &&
                        lit_value(arena_.lits(ref)[0]) == Value::True;
    if (removed < target && !locked && h.lbd > 2 && arena_.size(ref) > 2) {
      if (proof_) proof_->del(arena_.lits(ref), arena_.size(ref));
      detach(ref);
      arena_.mark_free(ref);
      removed++;
      stats_.deleted++;
    } else {
      learnts_[keep++] = ref;
    }
  }
  learnts_.resize(keep);
}

bool Solver::simplify_root() {
  if (!trail_lim_.empty()) return true;
  if (propagate() != CREF_UNDEF) return false;

  auto strip = [this](std::vector<int32_t>& list) {
    size_t keep = 0;
    for (int32_t ref : list) {
      const Lit* ls = arena_.lits(ref);
      const uint32_t size = arena_.size(ref);
      bool satisfied = false;
      for (uint32_t i = 0; i < size; i++) {
        if (lit_value(ls[i]) == Value::True) { satisfied = true; break; }
      }
      if (satisfied) {
        // Deletion is always sound in DRAT: removing a clause can only weaken
        // the formula, so the checker accepts it unconditionally.
        if (proof_) proof_->del(ls, size);
        detach(ref);
        arena_.mark_free(ref);
        stats_.deleted++;
      } else {
        list[keep++] = ref;
      }
    }
    list.resize(keep);
  };

  strip(learnts_);
  strip(clauses_);
  return true;
}

// ---- search ---------------------------------------------------------------

double Solver::luby(double y, int x) {
  // The Luby sequence without materialising it: find the subsequence x lands
  // in, then recurse into it.
  int size, seq;
  for (size = 1, seq = 0; size < x + 1; seq++, size = 2 * size + 1) {}
  while (size - 1 != x) {
    size = (size - 1) >> 1;
    seq--;
    x = x % size;
  }
  return std::pow(y, seq);
}

Lit Solver::pick_branch() {
  Var next = -1;
  while (!heap_empty()) {
    const Var v = heap_pop();
    if (assign_[v] == Value::Undef) { next = v; break; }
  }
  if (next < 0) return LIT_UNDEF;
  stats_.decisions++;
  return mk_lit(next, saved_phase_[next] != 0);
}

std::vector<int> Solver::model() const {
  std::vector<int> out;
  out.reserve(assign_.size());
  for (size_t v = 0; v < assign_.size(); v++) {
    const int d = static_cast<int>(v) + 1;
    out.push_back(assign_[v] == Value::False ? -d : d);
  }
  return out;
}

Status Solver::solve() {
  const auto started = std::chrono::steady_clock::now();
  auto finish = [&](Status s) {
    stats_.seconds = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - started).count();
    return s;
  };

  if (unsat_) return finish(Status::Unsat);
  // Decision levels run from 0 to num_vars inclusive, so the per-level stamp
  // array needs one more slot than there are variables.
  lbd_stamp_.assign(assign_.size() + 1, 0);
  if (!simplify_root()) {
    unsat_ = true;
    if (proof_) proof_->empty_clause();
    return finish(Status::Unsat);
  }

  std::vector<Lit> learnt;
  int restart_count = 0;
  uint64_t conflicts_at_restart = 0;
  double reduce_limit = opts_.reduce_first;

  while (true) {
    const int32_t conflict = propagate();

    if (conflict != CREF_UNDEF) {
      stats_.conflicts++;
      if (trail_lim_.empty()) {
        if (proof_) proof_->empty_clause();
        return finish(Status::Unsat);
      }

      int backtrack_level = 0;
      analyze(conflict, learnt, backtrack_level);

      // Emitted before the clause can take part in anything else. A proof whose
      // steps are out of order is not a proof.
      if (proof_) proof_->add(learnt);
      stats_.learned++;
      stats_.learned_lits += learnt.size();

      const uint32_t lbd = compute_lbd(learnt);
      backtrack(backtrack_level);

      if (learnt.size() == 1) {
        enqueue(learnt[0], CREF_UNDEF);
      } else {
        const int32_t ref = arena_.alloc(learnt, true, lbd);
        learnts_.push_back(ref);
        attach(ref);
        bump_clause(ref);
        enqueue(learnt[0], ref);
      }

      decay_vars();
      clause_inc_ /= opts_.clause_decay;

      if (opts_.conflict_budget && stats_.conflicts >= opts_.conflict_budget) {
        return finish(Status::Unknown);
      }

      const double interval = luby(2.0, restart_count) * opts_.restart_base;
      if (stats_.conflicts - conflicts_at_restart >= interval) {
        restart_count++;
        conflicts_at_restart = stats_.conflicts;
        stats_.restarts++;
        backtrack(0);
      }

      if (static_cast<double>(learnts_.size()) >= reduce_limit) {
        reduce_db();
        reduce_limit += opts_.reduce_step;
      }
    } else {
      const Lit next = pick_branch();
      if (next == LIT_UNDEF) return finish(Status::Sat);
      trail_lim_.push_back(static_cast<int32_t>(trail_.size()));
      enqueue(next, CREF_UNDEF);
    }
  }
}

}  // namespace sat
