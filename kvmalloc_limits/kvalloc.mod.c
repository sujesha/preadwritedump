#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x1792d969, "module_layout" },
	{ 0x863f935b, "remove_proc_entry" },
	{ 0xe4c5e55e, "create_proc_entry" },
	{ 0xedb4a6cd, "__mutex_init" },
	{ 0x7ec9bfbc, "strncpy" },
	{ 0xbfec4eb4, "kmem_cache_alloc_trace" },
	{ 0x868cc109, "kmalloc_caches" },
	{ 0x91715312, "sprintf" },
	{ 0x37a0cba, "kfree" },
	{ 0x5a34a45c, "__kmalloc" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x7012c74, "mutex_unlock" },
	{ 0x999e8297, "vfree" },
	{ 0x27e1a049, "printk" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x20000329, "simple_strtoul" },
	{ 0x4f6b400b, "_copy_from_user" },
	{ 0xa7906b18, "mutex_lock" },
	{ 0xb4390f9a, "mcount" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "85A91306A2E334A5CB91DC2");
