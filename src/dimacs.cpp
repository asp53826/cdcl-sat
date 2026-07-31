// DIMACS CNF reader.
//
// Hand-rolled rather than iostream: the SATLIB and competition files run to
// hundreds of megabytes and >> on an ifstream spends more time in locale
// handling than the solver spends on small instances.
//
// The header line is treated as a hint, not as truth. Files in the wild
// routinely under-declare the variable count, and refusing to solve them is
// less useful than growing the variable table.

#include "sat/dimacs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sat {

namespace {

struct Reader {
  std::FILE* f;
  std::vector<char> buf;
  size_t pos = 0, len = 0;

  explicit Reader(std::FILE* file) : f(file), buf(1 << 16) {}

  int get() {
    if (pos >= len) {
      len = std::fread(buf.data(), 1, buf.size(), f);
      pos = 0;
      if (len == 0) return EOF;
    }
    return static_cast<unsigned char>(buf[pos++]);
  }

  int peek() {
    if (pos >= len) {
      len = std::fread(buf.data(), 1, buf.size(), f);
      pos = 0;
      if (len == 0) return EOF;
    }
    return static_cast<unsigned char>(buf[pos]);
  }
};

void skip_space(Reader& r) {
  int c = r.peek();
  while (c != EOF && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
    r.get();
    c = r.peek();
  }
}

void skip_line(Reader& r) {
  int c = r.get();
  while (c != EOF && c != '\n') c = r.get();
}

bool read_int(Reader& r, long& out) {
  skip_space(r);
  int c = r.peek();
  if (c == EOF) return false;
  bool negative = false;
  if (c == '-' || c == '+') {
    negative = (c == '-');
    r.get();
    c = r.peek();
  }
  if (c < '0' || c > '9') return false;
  long value = 0;
  while (c >= '0' && c <= '9') {
    value = value * 10 + (c - '0');
    r.get();
    c = r.peek();
  }
  out = negative ? -value : value;
  return true;
}

}  // namespace

ParseResult read_dimacs(const std::string& path, Solver& solver) {
  ParseResult result;
  std::FILE* f = std::fopen(path.c_str(), "r");
  if (!f) {
    result.error = "cannot open " + path;
    return result;
  }
  Reader r(f);

  std::vector<Lit> clause;
  while (true) {
    skip_space(r);
    const int c = r.peek();
    if (c == EOF) break;

    if (c == 'c') {
      skip_line(r);
      continue;
    }
    if (c == 'p') {
      // "p cnf <vars> <clauses>"
      skip_line(r);
      continue;
    }

    long value = 0;
    if (!read_int(r, value)) {
      result.error = "unexpected character in " + path;
      std::fclose(f);
      return result;
    }

    if (value == 0) {
      if (!solver.add_clause(clause)) result.trivially_unsat = true;
      result.clauses++;
      clause.clear();
      continue;
    }

    const long v = value < 0 ? -value : value;
    if (v > (1L << 28)) {
      result.error = "implausible variable index";
      std::fclose(f);
      return result;
    }
    solver.reserve_vars(static_cast<int>(v));
    clause.push_back(mk_lit(static_cast<Var>(v - 1), value < 0));
  }

  // A trailing clause with no terminating zero is malformed, but every solver
  // accepts it and so does this one.
  if (!clause.empty()) {
    if (!solver.add_clause(clause)) result.trivially_unsat = true;
    result.clauses++;
  }

  result.vars = solver.num_vars();
  std::fclose(f);
  return result;
}

bool verify_model(const std::string& path, const std::vector<int>& model,
                  std::string& why) {
  std::FILE* f = std::fopen(path.c_str(), "r");
  if (!f) {
    why = "cannot reopen " + path;
    return false;
  }
  Reader r(f);

  std::vector<int8_t> value(model.size() + 1, 0);
  for (int lit : model) {
    const size_t v = lit < 0 ? static_cast<size_t>(-lit) : static_cast<size_t>(lit);
    if (v < value.size()) value[v] = lit < 0 ? -1 : 1;
  }

  std::vector<int> clause;
  size_t index = 0;
  bool ok = true;

  while (ok) {
    skip_space(r);
    const int c = r.peek();
    if (c == EOF) break;
    if (c == 'c' || c == 'p') { skip_line(r); continue; }

    long lit = 0;
    if (!read_int(r, lit)) break;
    if (lit != 0) { clause.push_back(static_cast<int>(lit)); continue; }

    index++;
    bool satisfied = false;
    for (int l : clause) {
      const size_t v = l < 0 ? static_cast<size_t>(-l) : static_cast<size_t>(l);
      if (v >= value.size()) continue;
      if ((l > 0 && value[v] == 1) || (l < 0 && value[v] == -1)) {
        satisfied = true;
        break;
      }
    }
    if (!satisfied) {
      why = "clause " + std::to_string(index) + " is not satisfied";
      ok = false;
    }
    clause.clear();
  }

  std::fclose(f);
  return ok;
}

}  // namespace sat
