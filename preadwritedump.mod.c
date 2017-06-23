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
	{ 0x4c46c04, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0xbb323702, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0x4c4fef19, __VMLINUX_SYMBOL_STR(kernel_stack) },
	{ 0x619cb7dd, __VMLINUX_SYMBOL_STR(simple_read_from_buffer) },
	{ 0xa906277b, __VMLINUX_SYMBOL_STR(debugfs_create_dir) },
	{ 0x754d539c, __VMLINUX_SYMBOL_STR(strlen) },
	{ 0x9f82e58f, __VMLINUX_SYMBOL_STR(relay_file_operations) },
	{ 0xc8b57c27, __VMLINUX_SYMBOL_STR(autoremove_wake_function) },
	{ 0x20000329, __VMLINUX_SYMBOL_STR(simple_strtoul) },
	{ 0xcb921acb, __VMLINUX_SYMBOL_STR(filp_close) },
	{ 0x56f39729, __VMLINUX_SYMBOL_STR(relay_flush) },
	{ 0xe7be439a, __VMLINUX_SYMBOL_STR(debugfs_create_file) },
	{ 0x54efb5d6, __VMLINUX_SYMBOL_STR(cpu_number) },
	{ 0x1b9aca3f, __VMLINUX_SYMBOL_STR(jprobe_return) },
	{ 0x91715312, __VMLINUX_SYMBOL_STR(sprintf) },
	{ 0xb1bedeb7, __VMLINUX_SYMBOL_STR(register_jprobe) },
	{ 0x922fd06, __VMLINUX_SYMBOL_STR(relay_switch_subbuf) },
	{ 0xe4e8a2d5, __VMLINUX_SYMBOL_STR(kthread_create_on_node) },
	{ 0xfe7c4287, __VMLINUX_SYMBOL_STR(nr_cpu_ids) },
	{ 0x2bcc76e, __VMLINUX_SYMBOL_STR(vfs_read) },
	{ 0xd1a760ee, __VMLINUX_SYMBOL_STR(relay_close) },
	{ 0x608106e7, __VMLINUX_SYMBOL_STR(current_task) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0x20c55ae0, __VMLINUX_SYMBOL_STR(sscanf) },
	{ 0x1f3147c7, __VMLINUX_SYMBOL_STR(kthread_stop) },
	{ 0x449ad0a7, __VMLINUX_SYMBOL_STR(memcmp) },
	{ 0x9166fada, __VMLINUX_SYMBOL_STR(strncpy) },
	{ 0x3235456f, __VMLINUX_SYMBOL_STR(debugfs_remove) },
	{ 0x1e6d26a8, __VMLINUX_SYMBOL_STR(strstr) },
	{ 0x6a398611, __VMLINUX_SYMBOL_STR(pid_task) },
	{ 0x3ec77f2b, __VMLINUX_SYMBOL_STR(generic_make_request) },
	{ 0x8aba28a3, __VMLINUX_SYMBOL_STR(relay_buf_full) },
	{ 0x78764f4e, __VMLINUX_SYMBOL_STR(pv_irq_ops) },
	{ 0xe16b0d30, __VMLINUX_SYMBOL_STR(unregister_jprobe) },
	{ 0xf0fdf6cb, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0x79bcd2f2, __VMLINUX_SYMBOL_STR(wake_up_process) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0xcc5005fe, __VMLINUX_SYMBOL_STR(msleep_interruptible) },
	{ 0x7149fc2a, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0xce46e140, __VMLINUX_SYMBOL_STR(ktime_get_ts) },
	{ 0xcf21d241, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0xb3f7646e, __VMLINUX_SYMBOL_STR(kthread_should_stop) },
	{ 0xcbad372d, __VMLINUX_SYMBOL_STR(init_pid_ns) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x69acdf38, __VMLINUX_SYMBOL_STR(memcpy) },
	{ 0x5c8b5ce8, __VMLINUX_SYMBOL_STR(prepare_to_wait) },
	{ 0xf7d1b21, __VMLINUX_SYMBOL_STR(send_sig_info) },
	{ 0xf95715e4, __VMLINUX_SYMBOL_STR(put_page) },
	{ 0xfa66f77c, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0x116b1996, __VMLINUX_SYMBOL_STR(relay_open) },
	{ 0x28318305, __VMLINUX_SYMBOL_STR(snprintf) },
	{ 0x4506dc02, __VMLINUX_SYMBOL_STR(bdget) },
	{ 0x4f6b400b, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0xaf6599d5, __VMLINUX_SYMBOL_STR(find_pid_ns) },
	{ 0x87b256e, __VMLINUX_SYMBOL_STR(bdput) },
	{ 0x6dce86ce, __VMLINUX_SYMBOL_STR(read_dev_sector) },
	{ 0xef6dd803, __VMLINUX_SYMBOL_STR(vfs_write) },
	{ 0xe914e41e, __VMLINUX_SYMBOL_STR(strcpy) },
	{ 0xc6eb69de, __VMLINUX_SYMBOL_STR(filp_open) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "39F23CAD895FC355AD48693");
