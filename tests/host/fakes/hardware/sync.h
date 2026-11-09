#pragma once
static inline void __dmb(void) { __sync_synchronize(); }
