// Runs the Framework's SnapshotBuffer test module (code/tests/modules).
#define UNIT_MAX_MODULES 1
#include "unit.h"

#include "modules/snapshot_buffer_ut.h"

int main() {
    UNIT_CREATE("SnapshotBufferTests");
    UNIT_MODULE(snapshot_buffer);
    return UNIT_RUN();
}
