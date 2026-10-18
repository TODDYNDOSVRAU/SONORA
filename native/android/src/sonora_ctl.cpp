#include "sonora_shared_state.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <string>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr int kAtomicOrder = __ATOMIC_SEQ_CST;

uint32_t floatToBits(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit IEEE-754");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float bitsToFloat(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

uint32_t loadWord(const uint32_t *word) {
  return __atomic_load_n(word, kAtomicOrder);
}

void storeWord(uint32_t *word, uint32_t value) {
  __atomic_store_n(word, value, kAtomicOrder);
}

#ifdef __ANDROID__
int openControlDirectory() {
  if (std::strcmp(SONORA_CONTROL_FILE, "/data/vendor/audio/sonora_state.bin") != 0) {
    errno = EPERM;
    return -1;
  }
  const struct passwd *audioUser = getpwnam("audio");
  const struct group *audioGroup = getgrnam("audio");
  if (!audioUser || !audioGroup) { errno = ENOENT; return -1; }

  const int dataFd = open("/data", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  if (dataFd < 0) return -1;
  const int vendorFd = openat(dataFd, "vendor", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  const int vendorError = errno;
  close(dataFd);
  if (vendorFd < 0) { errno = vendorError; return -1; }
  const int audioFd = openat(vendorFd, "audio", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  const int audioError = errno;
  close(vendorFd);
  if (audioFd < 0) { errno = audioError; return -1; }

  struct stat info{};
  const bool trustedDirectory = fstat(audioFd, &info) == 0 && S_ISDIR(info.st_mode) &&
      info.st_uid == audioUser->pw_uid && info.st_gid == audioGroup->gr_gid &&
      (info.st_mode & 0777) == 0770;
  if (!trustedDirectory) {
    close(audioFd);
    errno = EPERM;
    return -1;
  }
  return audioFd;
}

constexpr const char *kControlName = "sonora_state.bin";
#else
int openControlDirectory(char *baseName, size_t baseNameCapacity) {
  const std::string path = SONORA_CONTROL_FILE;
  const size_t slash = path.find_last_of('/');
  const std::string directory = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
  const std::string basename = slash == std::string::npos ? path : path.substr(slash + 1);
  if (basename.empty() || basename == "." || basename == ".." || basename.size() >= baseNameCapacity) {
    errno = EINVAL;
    return -1;
  }
  std::memcpy(baseName, basename.c_str(), basename.size() + 1);
  return open(directory.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
}
#endif

int openState(bool create, int *fdOut, SonoraSharedState **stateOut, bool *createdOut = nullptr) {
  if (!fdOut || !stateOut) { errno = EINVAL; return -1; }
#ifdef __ANDROID__
  const int directoryFd = openControlDirectory();
  const char *baseName = kControlName;
#else
  char baseNameBuffer[NAME_MAX + 1]{};
  const int directoryFd = openControlDirectory(baseNameBuffer, sizeof(baseNameBuffer));
  const char *baseName = baseNameBuffer;
#endif
  if (directoryFd < 0) return -1;

  const int flags = O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
  bool created = false;
  int fd = -1;
  if (create) {
    fd = openat(directoryFd, baseName, flags | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) created = true;
    else if (errno == EEXIST) fd = openat(directoryFd, baseName, flags);
  } else {
    fd = openat(directoryFd, baseName, flags);
  }
  const int openError = errno;
  close(directoryFd);
  if (fd < 0) { errno = openError; return -1; }
  if (flock(fd, LOCK_EX) != 0) { close(fd); return -1; }

  struct stat info{};
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1) {
    flock(fd, LOCK_UN); close(fd); errno = EPERM; return -1;
  }

#ifdef __ANDROID__
  const struct group *audioGroup = getgrnam("audio");
  if (!audioGroup) { flock(fd, LOCK_UN); close(fd); errno = ENOENT; return -1; }
  if (created) {
    if (fchown(fd, 0, audioGroup->gr_gid) != 0 || fchmod(fd, 0640) != 0 ||
        ftruncate(fd, static_cast<off_t>(SONORA_STATE_BYTES)) != 0) {
      flock(fd, LOCK_UN); close(fd); return -1;
    }
  } else if (info.st_size != static_cast<off_t>(SONORA_STATE_BYTES) || info.st_uid != 0 ||
             info.st_gid != audioGroup->gr_gid || (info.st_mode & 0777) != 0640) {
    flock(fd, LOCK_UN); close(fd); errno = EPERM; return -1;
  }
#else
  if (created) {
    if (fchmod(fd, 0640) != 0 || ftruncate(fd, static_cast<off_t>(SONORA_STATE_BYTES)) != 0) {
      flock(fd, LOCK_UN); close(fd); return -1;
    }
  } else if (info.st_size != static_cast<off_t>(SONORA_STATE_BYTES)) {
    flock(fd, LOCK_UN); close(fd); errno = EINVAL; return -1;
  }
#endif

  if (created) {
    struct stat afterCreate{};
    if (fstat(fd, &afterCreate) != 0 || !S_ISREG(afterCreate.st_mode) || afterCreate.st_nlink != 1 ||
        afterCreate.st_size != static_cast<off_t>(SONORA_STATE_BYTES)) {
      flock(fd, LOCK_UN); close(fd); errno = EPERM; return -1;
    }
  }

  void *mapping = mmap(nullptr, SONORA_STATE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) { flock(fd, LOCK_UN); close(fd); return -1; }
  *fdOut = fd;
  *stateOut = static_cast<SonoraSharedState *>(mapping);
  if (createdOut) *createdOut = created;
  return 0;
}

void closeState(int fd, SonoraSharedState *state) {
  if (state) { msync(state, SONORA_STATE_BYTES, MS_SYNC); munmap(state, SONORA_STATE_BYTES); }
  if (fd >= 0) { flock(fd, LOCK_UN); close(fd); }
}

bool readSnapshot(const SonoraSharedState *state, uint32_t *sequenceOut = nullptr) {
  if (!state) return false;
  for (int attempt = 0; attempt < 4; ++attempt) {
    const uint32_t before = loadWord(&state->sequence);
    if (before & 1u) continue;
    const uint32_t magic = loadWord(&state->magic);
    const uint32_t version = loadWord(&state->version);
    const uint32_t enabled = loadWord(&state->enabled);
    const uint32_t position = loadWord(&state->position);
    const float preamp = bitsToFloat(loadWord(&state->preamp_db_bits));
    bool gainsValid = true;
    for (size_t i = 0; i < SONORA_EQ_BANDS; ++i) {
      const float gain = bitsToFloat(loadWord(&state->eq_db_bits[i]));
      if (!std::isfinite(gain) || gain < -12.0f || gain > 12.0f) gainsValid = false;
    }
    const uint32_t after = loadWord(&state->sequence);
    if (before != after || (after & 1u)) continue;
    if (magic != SONORA_STATE_MAGIC || version != SONORA_STATE_VERSION || enabled > 1 || position > 4 ||
        !std::isfinite(preamp) || preamp < -12.0f || preamp > 6.0f || !gainsValid) return false;
    if (sequenceOut) *sequenceOut = after;
    return true;
  }
  return false;
}

void resetState(SonoraSharedState *state) {
  storeWord(&state->sequence, 1u);
  storeWord(&state->magic, SONORA_STATE_MAGIC);
  storeWord(&state->version, SONORA_STATE_VERSION);
  storeWord(&state->enabled, 0u);
  storeWord(&state->position, 0u);
  storeWord(&state->preamp_db_bits, floatToBits(0.0f));
  for (size_t i = 0; i < SONORA_EQ_BANDS; ++i) storeWord(&state->eq_db_bits[i], floatToBits(0.0f));
  storeWord(&state->sequence, 2u);
}

bool beginWrite(SonoraSharedState *state, uint32_t *evenSequence) {
  uint32_t sequence = 0;
  if (!readSnapshot(state, &sequence) || (sequence & 1u)) return false;
  *evenSequence = sequence + 2u;
  storeWord(&state->sequence, sequence + 1u);
  return true;
}

void endWrite(SonoraSharedState *state, uint32_t evenSequence) {
  storeWord(&state->sequence, evenSequence);
}

int initialize() {
  int fd = -1;
  SonoraSharedState *state = nullptr;
  bool created = false;
  if (openState(true, &fd, &state, &created) != 0) return 2;
  if (created || !readSnapshot(state)) resetState(state);
  closeState(fd, state);
  return 0;
}

int parseFloat(const char *text, float minimum, float maximum, float *out) {
  if (!text || !out) return -1;
  char *end = nullptr;
  errno = 0;
  const float value = std::strtof(text, &end);
  if (errno != 0 || end == text || *end != '\0' || !std::isfinite(value) || value < minimum || value > maximum) return -1;
  *out = value;
  return 0;
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

int apply(int argc, char **argv) {
  if (argc != 6) return 2;
  if (std::strcmp(argv[2], "0") != 0 && std::strcmp(argv[2], "1") != 0) return 2;
  uint32_t position = 0;
  float preamp = 0.0f;
  if (positionFromName(argv[3], &position) != 0 || parseFloat(argv[4], -12.0f, 6.0f, &preamp) != 0) return 2;
  float gains[SONORA_EQ_BANDS]{};
  char list[256]{};
  if (std::strlen(argv[5]) >= sizeof(list)) return 2;
  std::strcpy(list, argv[5]);
  char *cursor = list;
  for (size_t i = 0; i < SONORA_EQ_BANDS; ++i) {
    char *comma = std::strchr(cursor, ',');
    if (i + 1 < SONORA_EQ_BANDS && !comma) return 2;
    if (i + 1 == SONORA_EQ_BANDS && comma) return 2;
    if (comma) *comma = '\0';
    if (parseFloat(cursor, -12.0f, 12.0f, &gains[i]) != 0) return 2;
    cursor = comma ? comma + 1 : cursor + std::strlen(cursor);
  }

  int fd = -1;
  SonoraSharedState *state = nullptr;
  if (openState(false, &fd, &state) != 0 || !readSnapshot(state)) {
    if (state) closeState(fd, state); else if (fd >= 0) closeState(fd, nullptr);
    return 3;
  }
  uint32_t generation = 0;
  if (!beginWrite(state, &generation)) { closeState(fd, state); return 3; }
  storeWord(&state->enabled, argv[2][0] == '1' ? 1u : 0u);
  storeWord(&state->position, position);
  storeWord(&state->preamp_db_bits, floatToBits(preamp));
  for (size_t i = 0; i < SONORA_EQ_BANDS; ++i) storeWord(&state->eq_db_bits[i], floatToBits(gains[i]));
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
    if (openState(false, &fd, &state) != 0 || !readSnapshot(state)) {
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
