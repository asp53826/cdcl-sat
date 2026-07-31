#include "sat/proof.h"

#include <cstdlib>

namespace sat {

namespace {
// DIMACS literals are signed and 1-based; the internal encoding is unsigned and
// 0-based. This is the only place in the proof path that knows that.
inline int to_dimacs(Lit l) {
  int v = var_of(l) + 1;
  return sign_of(l) ? -v : v;
}
}  // namespace

Proof::Proof(const std::string& path) {
  file_ = std::fopen(path.c_str(), "w");
  buf_.reserve(1 << 16);
}

Proof::~Proof() { close(); }

void Proof::write_lits(const Lit* lits, uint32_t n) {
  char tmp[16];
  for (uint32_t i = 0; i < n; i++) {
    int d = to_dimacs(lits[i]);
    int len = std::snprintf(tmp, sizeof(tmp), "%d ", d);
    buf_.append(tmp, len);
  }
  buf_.append("0\n");
  lines_++;
  // 64 KiB is about where the syscall cost stops showing up in the profile.
  // Proofs for hard instances run to hundreds of megabytes and an unbuffered
  // fprintf per literal costs more than the search.
  if (buf_.size() >= (1 << 16)) {
    std::fwrite(buf_.data(), 1, buf_.size(), file_);
    buf_.clear();
  }
}

void Proof::add(const std::vector<Lit>& lits) {
  if (!file_) return;
  write_lits(lits.data(), static_cast<uint32_t>(lits.size()));
}

void Proof::del(const Lit* lits, uint32_t n) {
  if (!file_) return;
  buf_.append("d ");
  write_lits(lits, n);
}

void Proof::empty_clause() {
  if (!file_) return;
  buf_.append("0\n");
  lines_++;
}

void Proof::close() {
  if (!file_) return;
  if (!buf_.empty()) {
    std::fwrite(buf_.data(), 1, buf_.size(), file_);
    buf_.clear();
  }
  std::fclose(file_);
  file_ = nullptr;
}

}  // namespace sat
