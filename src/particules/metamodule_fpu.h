#pragma once

#include <cstdint>

namespace particules {

// Sets the ARM FPU's flush-to-zero and default-NaN mode bits (FZ, DN) in
// FPSCR. This intentionally mutates thread-wide FPU state for the lifetime
// of the audio thread it's called from — it is not scoped to this module
// or restored afterward. No-op on non-ARM / non-MetaModule builds.
inline void EnableMetaModuleFlushToZero() {
#if defined(METAMODULE) && defined(__arm__)
	std::uint32_t fpscr;
	__asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
	fpscr |= (1u << 24) | (1u << 25);
	__asm__ volatile("vmsr fpscr, %0" : : "r"(fpscr));
#endif
}

}  // namespace particules
