#include <safetyhook.hpp>
#include <cstdio>

namespace {
SafetyHookInline hook;
volatile int originalCalls = 0;
int hookCalls = 0;

#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
int Original(int value)
{
    originalCalls = originalCalls + 1;
    return value * 7 + 3;
}

int Detour(int value)
{
    ++hookCalls;
    return hook.call<int>(value) + 100;
}
} // namespace

int main()
{
    // Volatile indirection prevents optimizing calls around the patched entry.
    int (*volatile invoke)(int) = &Original;
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto created = safetyhook::InlineHook::create(&Original, &Detour,
                                                     safetyhook::InlineHook::StartDisabled);
        if (!created)
            return std::puts("FAIL: create detour"), 1;
        hook = std::move(*created);
        if (invoke(2) != 17 || !hook.enable() || invoke(2) != 117 || !hook.disable())
            return std::puts("FAIL: original forwarding / enable / disable"), 1;
        hook.reset();
        if (invoke(2) != 17)
            return std::puts("FAIL: original function restoration"), 1;
    }
    if (hookCalls != 3 || originalCalls != 9)
        return std::puts("FAIL: original must be called exactly once"), 1;
    std::puts("Detour install, original forwarding, unload and reload passed.");
    return 0;
}
