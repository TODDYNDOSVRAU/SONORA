#include "sonora_shared_state.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool parseFloat(const char *text, float minimum, float maximum, float *out) {
  if (!text || !out) return false;
  char *end = nullptr;
  errno = 0;
  const float value = std::strtof(text, &end);
  if (errno != 0 || end == text || *end != '\0' || !std::isfinite(value) || value < minimum || value > maximum) return false;
  *out = value;
  return true;
}

int positionFromName(const char *name, uint32_t *position) {
  if (!name || !position) return -1;
  if (std::strcmp(name, "center") == 0) *position = 0;
  else if (std::strcmp(name, "front") == 0) *position = 1;
  else if (std::strcmp(name, "back") == 0) *position = 2;
  else if (std::strcmp(name, "right") == 0) *position = 3;
  else if (std::strcmp(name, "left") == 0) *position = 4;
  else return -1;
  return 0;
}

int openState(bool create, int *fdOut, SonoraSharedState **stateOut) {
  const int flags = O_RDWR | O_CLOEXEC | (create ? O_CREAT : 0);
  const int fd = open(SONORA_CONTROL_FILE, flags, 0660);
  if (fd < 0) return -1;
  if (flock(fd, LOCK_EX) != 0) { close(fd); return -1; }
  struct stat info{};
  if (fstat(fd, &info) != 0) {
    flock(fd, LOCK_UN); close(fd); return -1;
  }
  if (create && info.st_size < static_cast<off_t>(sizeof(SonoraSharedState))) {
    if (ftruncate(fd, sizeof(SonoraSharedState)) != 0) { flock(fd, LOCK_UN); close(fd); return -1; }
  } else if (info.st_size < static_cast<off_t>(sizeof(SonoraSharedState))) {
    flock(fd, LOCK_UN); close(fd); return -1;
  }
  void *mapping = mmap(nullptr, sizeof(SonoraSharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) { flock(fd, LOCK_UN); close(fd); return -1; }
  *fdOut = fd;
  *stateOut = static_cast<SonoraSharedState *>(mapping);
  return 0;
}

void closeState(int fd, SonoraSharedState *state) {
  if (state) { msync(state, sizeof(*state), MS_SYNC); munmap(state, sizeof(*state)); }
  if (fd >= 0) { flock(fd, LOCK_UN); close(fd); }
}

bool validState(const SonoraSharedState *state) {
  if (!state || state->magic != SONORA_STATE_MAGIC || state->version != SONORA_STATE_VERSION ||
      (state->sequence & 1u) != 0 || state->enabled > 1 || state->position > 4 ||
      !std::isfinite(state->preamp_db) || state->preamp_db < -12.0f || state->preamp_db > 6.0f) return false;
  for (float gain : state->eq_db) {
    if (!std::isfinite(gain) || gain < -12.0f || gain > 12.0f) return false;
  }
  return true;
}

void beginWrite(SonoraSharedState *state, uint32_t *evenSequence) {
  uint32_t sequence = __atomic_load_n(&state->sequence, __ATOMIC_RELAXED);
  if (sequence & 1u) ++sequence;
  *evenSequence = sequence + 2;
  __atomic_store_n(&state->sequence, sequence + 1, __ATOMIC_RELEASE);
}

void endWrite(SonoraSharedState *state, uint32_t evenSequence) {
  __atomic_store_n(&state->sequence, evenSequence, __ATOMIC_RELEASE);
}

int initialize() {
  int fd = -1;
  SonoraSharedState *state = nullptr;
  if (openState(true, &fd, &state) != 0) return 2;
  if (!validState(state)) {
    std::memset(state, 0, sizeof(*state));
    state->magic = SONORA_STATE_MAGIC;
    state->version = SONORA_STATE_VERSION;
    state->enabled = 0;
    state->position = 0;
    state->preamp_db = 0.0f;
    for (float &gain : state->eq_db) gain = 0.0f;
    __atomic_store_n(&state->sequence, 2u, __ATOMIC_RELEASE);
  }
  closeState(fd, state);
  return 0;
}

int apply(int argc, char **argv) {
  if (argc != 6) return 2;
  if (std::strcmp(argv[2], "0") != 0 && std::strcmp(argv[2], "1") != 0) return 2;
  uint32_t position = 0;
  float preamp = 0.0f;
  if (positionFromName(argv[3], &position) != 0 || !parseFloat(argv[4], -12.0f, 6.0f, &preamp)) return 2;
  float gains[10]{};
  char list[256]{};
  if (std::strlen(argv[5]) >= sizeof(list)) return 2;
  std::strcpy(list, argv[5]);
  char *cursor = list;
  for (size_t i = 0; i < 10; ++i) {
    char *comma = std::strchr(cursor, ',');
    if (i < 9 && !comma) return 2;
    if (i == 9 && comma) return 2;
    if (comma) *comma = '\0';
    if (!parseFloat(cursor, -12.0f, 12.0f, &gains[i])) return 2;
    cursor = comma ? comma + 1 : cursor + std::strlen(cursor);
  }

  int fd = -1;
  SonoraSharedState *state = nullptr;
  if (openState(false, &fd, &state) != 0 || !validState(state)) {
    if (state) closeState(fd, state); else if (fd >= 0) closeState(fd, nullptr);
    return 3;
  }
  uint32_t generation = 0;
  beginWrite(state, &generation);
  state->magic = SONORA_STATE_MAGIC;
  state->version = SONORA_STATE_VERSION;
  state->enabled = argv[2][0] == '1' ? 1u : 0u;
  state->position = position;
  state->preamp_db = preamp;
  for (size_t i = 0; i < 10; ++i) state->eq_db[i] = gains[i];
  endWrite(state, generation);
  closeState(fd, state);
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "init") == 0) return initialize();
  if (argc == 2 && std::strcmp(argv[1], "status") == 0) {
    int fd = -1;
    SonoraSharedState *state = nullptr;
    if (openState(false, &fd, &state) != 0 || !validState(state)) {
      if (state) closeState(fd, state); else if (fd >= 0) closeState(fd, nullptr);
      std::puts("CONTROL=0");
      return 1;
    }
    std::puts("CONTROL=1");
    closeState(fd, state);
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "apply") == 0) return apply(argc, argv);
  std::fprintf(stderr, "usage: sonora-ctl init|status|apply <0|1> <center|front|back|right|left> <preamp_db> <10 comma-separated EQ dB values>\n");
  return 2;
}
