#ifndef _KERNEL_SIGNAL_USR_H_
#define _KERNEL_SIGNAL_USR_H_


int send_kernel_signal_usr(void);
int get_task_send_signal(void);
int kernel_signal_usr_init(void);
int kernel_signal_usr_exit(void);


#endif /* _KERNEL_SIGNAL_USR_H_ */
