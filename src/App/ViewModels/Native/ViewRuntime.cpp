#include "StorageProjection.h"
namespace dreamscapes::vm {
ii::runtime::Runtime &viewRuntime() {
    // One bounded pool for pure presentation work across every view instance.
    // Fast local commands use QueryPort::send; they never wait on this queue.
    static ii::runtime::Runtime runtime({2, 64});
    return runtime;
}
} // namespace dreamscapes::vm
