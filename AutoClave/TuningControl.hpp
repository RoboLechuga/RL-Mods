#pragma once

namespace TuningControl
{
    // Call once from the existing worker thread after AsioPassthrough::Install().
    bool Initialize();

    // Call each worker-loop iteration.
    void Poll();

    // Call when the worker exits.
    void Shutdown();
}
