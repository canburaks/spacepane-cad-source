#include "CADWorker.hpp"
#include <Standard_Failure.hxx>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <mach/mach.h>
#include <new>
#include <signal.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

namespace {
// Initial kernel parsing is not cooperative. CPU and sampled RSS containment are
// process-wide; the parent additionally kills/reaps this request at its deadline.
class ResourceGuard {
  std::atomic<bool> complete{false};
  std::thread monitor;
public:
  ResourceGuard() {
    rlimit core{0, 0}; ::setrlimit(RLIMIT_CORE, &core);
    rlimit cpu{15, 16}; ::setrlimit(RLIMIT_CPU, &cpu);
    monitor = std::thread([this] {
      while (!complete.load(std::memory_order_relaxed)) {
        task_vm_info_data_t info{};
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (::task_info(mach_task_self(), TASK_VM_INFO,
                        reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS ||
            count < TASK_VM_INFO_REV1_COUNT)
          std::_Exit(static_cast<int>(cad::Exit::internal));
        if (info.phys_footprint > 512ULL * 1024 * 1024)
          std::_Exit(static_cast<int>(cad::Exit::budget));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
  }
  ~ResourceGuard() { complete.store(true, std::memory_order_relaxed); monitor.join(); }
};

std::string readBytes() {
  std::string bytes;
  bytes.reserve(cad::maximumInputBytes);
  char buffer[32768];
  for (;;) {
    const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) throw cad::Failure{cad::Exit::internal};
    if (!count) break;
    if (static_cast<std::size_t>(count) > cad::maximumInputBytes - bytes.size())
      throw cad::Failure{cad::Exit::budget};
    bytes.append(buffer, static_cast<std::size_t>(count));
  }
  return bytes;
}
}

int main(int argc, char** argv) {
  // Preserve only the protocol descriptor. All C/C++ kernel reports, including
  // printf and default messenger output, are discarded before reader construction.
  const int destination = ::dup(STDOUT_FILENO);
  const int discard = ::open("/dev/null", O_WRONLY);
  if (destination < 0 || discard < 0) return static_cast<int>(cad::Exit::internal);
  ::dup2(discard, STDOUT_FILENO); ::dup2(discard, STDERR_FILENO); ::close(discard);
  ::signal(SIGPIPE, SIG_IGN);
  ::signal(SIGXCPU, [](int) { std::_Exit(static_cast<int>(cad::Exit::budget)); });
  cad::Exit result = cad::Exit::internal;
  try {
    ResourceGuard guard;
    if (argc != 2 || (std::string(argv[1]) != "iges" && std::string(argv[1]) != "step"))
      throw cad::Failure{cad::Exit::unsupported};
    const bool isSTEP = std::string(argv[1]) == "step";
    const std::string bytes = readBytes();
    cad::preflight(bytes, isSTEP);
    cad::Mesh mesh = cad::importMesh(bytes, isSTEP);
    mesh.declaredUnit = cad::declaredLengthUnit(bytes, isSTEP);
    cad::writeMesh(destination, mesh);
    result = cad::Exit::success;
  } catch (const cad::Failure& failure) { result = failure.status; }
    catch (const std::bad_alloc&) { result = cad::Exit::budget; }
    catch (const Standard_Failure&) { result = cad::Exit::malformed; }
    catch (...) { result = cad::Exit::internal; }
  ::close(destination);
  return static_cast<int>(result);
}
