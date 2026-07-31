#include <cstdio>
#include <cstring>
#include <string>

#include "sat/dimacs.h"
#include "sat/proof.h"
#include "sat/solver.h"

namespace {

void usage() {
  std::printf(
      "usage: satsolve <file.cnf> [options]\n"
      "\n"
      "  --proof <file>        emit a DRAT proof (only meaningful on UNSAT)\n"
      "  --no-verify           skip re-reading the CNF to check the model\n"
      "  --no-minimize         disable conflict clause minimization\n"
      "  --no-phase-saving     always branch on the default phase\n"
      "  --restart-base <n>    Luby unit in conflicts (default 100)\n"
      "  --var-decay <f>       VSIDS decay (default 0.95)\n"
      "  --budget <n>          give up after n conflicts\n"
      "  --quiet               result line only\n"
      "\n"
      "exit codes follow the SAT competition: 10 satisfiable, 20\n"
      "unsatisfiable, 0 unknown, 1 error.\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 1;
  }

  std::string path;
  std::string proof_path;
  bool verify = true;
  bool quiet = false;
  sat::Options opts;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") { usage(); return 0; }
    else if (a == "--proof" && i + 1 < argc) proof_path = argv[++i];
    else if (a == "--no-verify") verify = false;
    else if (a == "--no-minimize") opts.minimize = false;
    else if (a == "--no-phase-saving") opts.phase_saving = false;
    else if (a == "--quiet") quiet = true;
    else if (a == "--restart-base" && i + 1 < argc) opts.restart_base = std::atoi(argv[++i]);
    else if (a == "--var-decay" && i + 1 < argc) opts.var_decay = std::atof(argv[++i]);
    else if (a == "--budget" && i + 1 < argc) opts.conflict_budget = std::strtoull(argv[++i], nullptr, 10);
    else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return 1;
    } else {
      path = a;
    }
  }

  if (path.empty()) { usage(); return 1; }

  sat::Solver solver(opts);
  sat::Proof* proof = nullptr;
  if (!proof_path.empty()) {
    proof = new sat::Proof(proof_path);
    if (!proof->ok()) {
      std::fprintf(stderr, "cannot write proof to %s\n", proof_path.c_str());
      return 1;
    }
    solver.set_proof(proof);
  }

  const sat::ParseResult parsed = sat::read_dimacs(path, solver);
  if (!parsed.ok()) {
    std::fprintf(stderr, "%s\n", parsed.error.c_str());
    delete proof;
    return 1;
  }

  const sat::Status status = solver.solve();
  const sat::Stats& st = solver.stats();

  if (proof) {
    proof->close();
  }

  if (!quiet) {
    std::printf("c vars           %d\n", parsed.vars);
    std::printf("c clauses        %zu\n", parsed.clauses);
    std::printf("c decisions      %llu\n", (unsigned long long)st.decisions);
    std::printf("c propagations   %llu\n", (unsigned long long)st.propagations);
    std::printf("c conflicts      %llu\n", (unsigned long long)st.conflicts);
    std::printf("c restarts       %llu\n", (unsigned long long)st.restarts);
    std::printf("c learned        %llu\n", (unsigned long long)st.learned);
    if (st.learned) {
      std::printf("c learned len    %.1f\n",
                  (double)st.learned_lits / (double)st.learned);
    }
    std::printf("c minimized lits %llu\n", (unsigned long long)st.minimized_lits);
    std::printf("c db reductions  %llu\n", (unsigned long long)st.reductions);
    std::printf("c deleted        %llu\n", (unsigned long long)st.deleted);
    if (proof) {
      std::printf("c proof lines    %llu\n", (unsigned long long)proof->lines());
    }
    std::printf("c seconds        %.3f\n", st.seconds);
    if (st.seconds > 0) {
      std::printf("c props/sec      %.0f\n", st.propagations / st.seconds);
    }
  }

  int code = 0;
  if (status == sat::Status::Sat) {
    if (verify) {
      std::string why;
      if (!sat::verify_model(path, solver.model(), why)) {
        std::fprintf(stderr, "MODEL CHECK FAILED: %s\n", why.c_str());
        delete proof;
        return 1;
      }
      if (!quiet) std::printf("c model verified against the source file\n");
    }
    std::printf("s SATISFIABLE\n");
    std::printf("v");
    for (int lit : solver.model()) std::printf(" %d", lit);
    std::printf(" 0\n");
    code = 10;
  } else if (status == sat::Status::Unsat) {
    std::printf("s UNSATISFIABLE\n");
    code = 20;
  } else {
    std::printf("s UNKNOWN\n");
    code = 0;
  }

  delete proof;
  return code;
}
