/* Borrowed from http://people.ee.ethz.ch/~arkeller/linux/kernel_user_space_howto.html#s6
 * 
 * Credit to Ariane Keller mailto:ariane.zerospam.keller@tik.ee.ethz.ch
 */

#include <linux/version.h>
#include <asm/siginfo.h>	//siginfo
#include <linux/rcupdate.h>	//rcu_read_lock
#include <linux/sched.h>	//find_task_by_pid, find_task_by_pid_ns
#include <linux/debugfs.h>
#include <linux/uaccess.h>


#define SIG_TEST 44	// we choose 44 as our signal number (real-time signals are in the range of 33 to 64)

struct dentry *signalfile;
struct siginfo usrinfo;
int usrpid;
DECLARE_WAIT_QUEUE_HEAD(usrpid_waitq);

int send_kernel_signal_usr(void)
{
	int ret;
	struct task_struct *t;

	printk(KERN_DEBUG "PDD:Preparing to get task_struct\n");
    rcu_read_lock();

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)))
    t = find_task_by_pid_type(PIDTYPE_PID, usrpid);
#elif ((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)))
    t = find_task_by_pid_ns(usrpid, &init_pid_ns); /* find task_struct associated with this pid*/
#else
    t = pid_task(find_pid_ns(usrpid, &init_pid_ns), PIDTYPE_PID);
#endif

    if(t == NULL){
        printk("no such pid\n");
        rcu_read_unlock();
        return -ENODEV;
    }
    rcu_read_unlock();
    ret = send_sig_info(SIG_TEST, &usrinfo, t);    /* send the signal */
    if (ret < 0) {
        printk("error sending signal\n");
        return ret;
    }
	printk(KERN_DEBUG "PDD:Sent signal number %d\n", SIG_TEST);
	return 0;
}

/* Fills task info into task_struct and sends signal */
int get_task_send_signal(void)
{
	int ret;
	printk(KERN_DEBUG "PDD:Get process task_struct and send signal\n");
    if (usrpid != 0)
    {
        /* send the signal */
        memset(&usrinfo, 0, sizeof(struct siginfo));
        usrinfo.si_signo = SIG_TEST;
        usrinfo.si_code = SI_QUEUE; /* this is bit of a trickery: SI_QUEUE is 
                                    normally used by sigqueue from user space,
                                    and kernel space should use SI_KERNEL. 
                                    But if SI_KERNEL is used the real_time data 
									is not delivered to the user space signal 
                                    handler function. */

        usrinfo.si_int = 1234;  /* real time signals may have 32 bits of data */
        ret = send_kernel_signal_usr();
        if (ret)
            return ret;
		return 0;
    }
	else
		return -1;
}

/* Just note the consumer process pid for now */
static ssize_t write_pid(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
	char mybuf[10] = {};
	/* read the value from user space */
	if(count > 10)
		return -EINVAL;
    if (copy_from_user(mybuf, buf, count))
    {
        printk(KERN_CRIT "Could not copy_from_user in write_pid\n");
        return -1;
    }
	if (strcmp(mybuf, ""))
		sscanf(mybuf, "%d", &usrpid);
	else
		usrpid = 0;
	printk(KERN_DEBUG "PDD:pid = %d\n", usrpid);

	if (usrpid == 0)
		wake_up(&usrpid_waitq);

	return count;
}

static const struct file_operations kernel_signal_usr_fops = {
	.write = write_pid,
};

int kernel_signal_usr_init(void)
{
	/* we need to know the pid of the user space process
 	 * -> we use debugfs for this. As soon as a pid is written to 
 	 * this file, a signal is sent to that pid
 	 */
	/* only root can write to this file (no read) */
	signalfile = debugfs_create_file("signalconfpid", 0200, NULL, NULL, 
				&kernel_signal_usr_fops);
	if (!signalfile)
		return -1;
	printk(KERN_DEBUG "PDD:Created the signalconfpid file\n");
	return 0;
}
void kernel_signal_usr_exit(void)
{
	printk(KERN_DEBUG "PDD:Removed the signalconfpid file\n");
	debugfs_remove(signalfile);

}

