// Ce que la bibliothèque C++ de Visual C++ fournirait : table virtuelle de type_info (référencée par les
// TypeDescriptor des exceptions) et operator delete des destructeurs virtuels.
extern "C" void *rcn_type_info_vtable[1] __asm__("??_7type_info@@6B@");
void *rcn_type_info_vtable[1];
void operator delete(void *, unsigned long long) noexcept {}
