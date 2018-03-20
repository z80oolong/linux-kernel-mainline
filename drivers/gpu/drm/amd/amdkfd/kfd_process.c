/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/mutex.h>
#include <linux/log2.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/amd-iommu.h>
#include <linux/notifier.h>
#include <linux/compat.h>
#include <linux/mman.h>

struct mm_struct;

#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_dbgmgr.h"
#include "kfd_iommu.h"

/*
 * List of struct kfd_process (field kfd_process).
 * Unique/indexed by mm_struct*
 */
DEFINE_HASHTABLE(kfd_processes_table, KFD_PROCESS_TABLE_SIZE);
static DEFINE_MUTEX(kfd_processes_mutex);

DEFINE_SRCU(kfd_processes_srcu);

static struct workqueue_struct *kfd_process_wq;

static struct kfd_process *find_process(const struct task_struct *thread);
static void kfd_process_ref_release(struct kref *ref);
static struct kfd_process *create_process(const struct task_struct *thread,
					struct file *filep);
static int kfd_process_init_cwsr(struct kfd_process *p, struct file *filep);

static void evict_process_worker(struct work_struct *work);
static void restore_process_worker(struct work_struct *work);


void kfd_process_create_wq(void)
{
	if (!kfd_process_wq)
		kfd_process_wq = alloc_workqueue("kfd_process_wq", 0, 0);
}

void kfd_process_destroy_wq(void)
{
	if (kfd_process_wq) {
		destroy_workqueue(kfd_process_wq);
		kfd_process_wq = NULL;
	}
}

struct kfd_process *kfd_create_process(struct file *filep)
{
	struct kfd_process *process;
	struct task_struct *thread = current;

	if (!thread->mm)
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm)
		return ERR_PTR(-EINVAL);

	/*
	 * take kfd processes mutex before starting of process creation
	 * so there won't be a case where two threads of the same process
	 * create two kfd_process structures
	 */
	mutex_lock(&kfd_processes_mutex);

	/* A prior open of /dev/kfd could have already created the process. */
	process = find_process(thread);
	if (process)
		pr_debug("Process already found\n");
	else
		process = create_process(thread, filep);

	mutex_unlock(&kfd_processes_mutex);

	return process;
}

struct kfd_process *kfd_get_process(const struct task_struct *thread)
{
	struct kfd_process *process;

	if (!thread->mm)
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm)
		return ERR_PTR(-EINVAL);

	process = find_process(thread);

	return process;
}

static struct kfd_process *find_process_by_mm(const struct mm_struct *mm)
{
	struct kfd_process *process;

	hash_for_each_possible_rcu(kfd_processes_table, process,
					kfd_processes, (uintptr_t)mm)
		if (process->mm == mm)
			return process;

	return NULL;
}

static struct kfd_process *find_process(const struct task_struct *thread)
{
	struct kfd_process *p;
	int idx;

	idx = srcu_read_lock(&kfd_processes_srcu);
	p = find_process_by_mm(thread->mm);
	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}

void kfd_unref_process(struct kfd_process *p)
{
	kref_put(&p->ref, kfd_process_ref_release);
}

static void kfd_process_destroy_pdds(struct kfd_process *p)
{
	struct kfd_process_device *pdd, *temp;

	list_for_each_entry_safe(pdd, temp, &p->per_device_data,
				 per_device_list) {
		pr_debug("Releasing pdd (topology id %d) for process (pasid %d)\n",
				pdd->dev->id, p->pasid);

		if (pdd->vm)
			pdd->dev->kfd2kgd->destroy_process_vm(
				pdd->dev->kgd, pdd->vm);

		list_del(&pdd->per_device_list);

		if (pdd->qpd.cwsr_kaddr)
			free_pages((unsigned long)pdd->qpd.cwsr_kaddr,
				get_order(KFD_CWSR_TBA_TMA_SIZE));

		kfree(pdd);
	}
}

/* No process locking is needed in this function, because the process
 * is not findable any more. We must assume that no other thread is
 * using it any more, otherwise we couldn't safely free the process
 * structure in the end.
 */
static void kfd_process_wq_release(struct work_struct *work)
{
	struct kfd_process *p = container_of(work, struct kfd_process,
					     release_work);

	kfd_iommu_unbind_process(p);

	kfd_process_destroy_pdds(p);
	dma_fence_put(p->ef);

	kfd_event_free_process(p);

	kfd_pasid_free(p->pasid);
	kfd_free_process_doorbells(p);

	mutex_destroy(&p->mutex);

	put_task_struct(p->lead_thread);

	kfree(p);
}

static void kfd_process_ref_release(struct kref *ref)
{
	struct kfd_process *p = container_of(ref, struct kfd_process, ref);

	INIT_WORK(&p->release_work, kfd_process_wq_release);
	queue_work(kfd_process_wq, &p->release_work);
}

static void kfd_process_destroy_delayed(struct rcu_head *rcu)
{
	struct kfd_process *p = container_of(rcu, struct kfd_process, rcu);

	kfd_unref_process(p);
}

static void kfd_process_notifier_release(struct mmu_notifier *mn,
					struct mm_struct *mm)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd = NULL;

	/*
	 * The kfd_process structure can not be free because the
	 * mmu_notifier srcu is read locked
	 */
	p = container_of(mn, struct kfd_process, mmu_notifier);
	if (WARN_ON(p->mm != mm))
		return;

	mutex_lock(&kfd_processes_mutex);
	hash_del_rcu(&p->kfd_processes);
	mutex_unlock(&kfd_processes_mutex);
	synchronize_srcu(&kfd_processes_srcu);

	cancel_delayed_work_sync(&p->eviction_work);
	cancel_delayed_work_sync(&p->restore_work);

	mutex_lock(&p->mutex);

	/* Iterate over all process device data structures and if the
	 * pdd is in debug mode, we should first force unregistration,
	 * then we will be able to destroy the queues
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		struct kfd_dev *dev = pdd->dev;

		mutex_lock(kfd_get_dbgmgr_mutex());
		if (dev && dev->dbgmgr && dev->dbgmgr->pasid == p->pasid) {
			if (!kfd_dbgmgr_unregister(dev->dbgmgr, p)) {
				kfd_dbgmgr_destroy(dev->dbgmgr);
				dev->dbgmgr = NULL;
			}
		}
		mutex_unlock(kfd_get_dbgmgr_mutex());
	}

	kfd_process_dequeue_from_all_devices(p);
	pqm_uninit(&p->pqm);

	/* Indicate to other users that MM is no longer valid */
	p->mm = NULL;

	mutex_unlock(&p->mutex);

	mmu_notifier_unregister_no_release(&p->mmu_notifier, mm);
	mmu_notifier_call_srcu(&p->rcu, &kfd_process_destroy_delayed);
}

static const struct mmu_notifier_ops kfd_process_mmu_notifier_ops = {
	.release = kfd_process_notifier_release,
};

static int kfd_process_init_cwsr(struct kfd_process *p, struct file *filep)
{
	unsigned long  offset;
	struct kfd_process_device *pdd = NULL;
	struct kfd_dev *dev = NULL;
	struct qcm_process_device *qpd = NULL;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		dev = pdd->dev;
		qpd = &pdd->qpd;
		if (!dev->cwsr_enabled || qpd->cwsr_kaddr)
			continue;
		offset = (dev->id | KFD_MMAP_RESERVED_MEM_MASK) << PAGE_SHIFT;
		qpd->tba_addr = (int64_t)vm_mmap(filep, 0,
			KFD_CWSR_TBA_TMA_SIZE, PROT_READ | PROT_EXEC,
			MAP_SHARED, offset);

		if (IS_ERR_VALUE(qpd->tba_addr)) {
			int err = qpd->tba_addr;

			pr_err("Failure to set tba address. error %d.\n", err);
			qpd->tba_addr = 0;
			qpd->cwsr_kaddr = NULL;
			return err;
		}

		memcpy(qpd->cwsr_kaddr, dev->cwsr_isa, dev->cwsr_isa_size);

		qpd->tma_addr = qpd->tba_addr + KFD_CWSR_TMA_OFFSET;
		pr_debug("set tba :0x%llx, tma:0x%llx, cwsr_kaddr:%p for pqm.\n",
			qpd->tba_addr, qpd->tma_addr, qpd->cwsr_kaddr);
	}

	return 0;
}

static struct kfd_process *create_process(const struct task_struct *thread,
					struct file *filep)
{
	struct kfd_process *process;
	int err = -ENOMEM;

	process = kzalloc(sizeof(*process), GFP_KERNEL);

	if (!process)
		goto err_alloc_process;

	process->pasid = kfd_pasid_alloc();
	if (process->pasid == 0)
		goto err_alloc_pasid;

	if (kfd_alloc_process_doorbells(process) < 0)
		goto err_alloc_doorbells;

	kref_init(&process->ref);

	mutex_init(&process->mutex);

	process->mm = thread->mm;

	/* register notifier */
	process->mmu_notifier.ops = &kfd_process_mmu_notifier_ops;
	err = mmu_notifier_register(&process->mmu_notifier, process->mm);
	if (err)
		goto err_mmu_notifier;

	hash_add_rcu(kfd_processes_table, &process->kfd_processes,
			(uintptr_t)process->mm);

	process->lead_thread = thread->group_leader;
	get_task_struct(process->lead_thread);

	INIT_LIST_HEAD(&process->per_device_data);

	kfd_event_init_process(process);

	err = pqm_init(&process->pqm, process);
	if (err != 0)
		goto err_process_pqm_init;

	/* init process apertures*/
	process->is_32bit_user_mode = in_compat_syscall();
	err = kfd_init_apertures(process);
	if (err != 0)
		goto err_init_apertures;

	INIT_DELAYED_WORK(&process->eviction_work, evict_process_worker);
	INIT_DELAYED_WORK(&process->restore_work, restore_process_worker);
	process->last_restore_timestamp = get_jiffies_64();

	err = kfd_process_init_cwsr(process, filep);
	if (err)
		goto err_init_cwsr;

	return process;

err_init_cwsr:
	kfd_process_destroy_pdds(process);
err_init_apertures:
	pqm_uninit(&process->pqm);
err_process_pqm_init:
	hash_del_rcu(&process->kfd_processes);
	synchronize_rcu();
	mmu_notifier_unregister_no_release(&process->mmu_notifier, process->mm);
err_mmu_notifier:
	mutex_destroy(&process->mutex);
	kfd_free_process_doorbells(process);
err_alloc_doorbells:
	kfd_pasid_free(process->pasid);
err_alloc_pasid:
	kfree(process);
err_alloc_process:
	return ERR_PTR(err);
}

struct kfd_process_device *kfd_get_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		if (pdd->dev == dev)
			return pdd;

	return NULL;
}

struct kfd_process_device *kfd_create_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	pdd = kzalloc(sizeof(*pdd), GFP_KERNEL);
	if (!pdd)
		return NULL;

	pdd->dev = dev;
	INIT_LIST_HEAD(&pdd->qpd.queues_list);
	INIT_LIST_HEAD(&pdd->qpd.priv_queue_list);
	pdd->qpd.dqm = dev->dqm;
	pdd->qpd.pqm = &p->pqm;
	pdd->qpd.evicted = 0;
	pdd->process = p;
	pdd->bound = PDD_UNBOUND;
	pdd->already_dequeued = false;
	list_add(&pdd->per_device_list, &p->per_device_data);

	/* Create the GPUVM context for this specific device */
	if (dev->kfd2kgd->create_process_vm(dev->kgd, &pdd->vm,
					    &p->kgd_process_info, &p->ef)) {
		pr_err("Failed to create process VM object\n");
		goto err_create_pdd;
	}
	return pdd;

err_create_pdd:
	list_del(&pdd->per_device_list);
	kfree(pdd);
	return NULL;
}

/*
 * Direct the IOMMU to bind the process (specifically the pasid->mm)
 * to the device.
 * Unbinding occurs when the process dies or the device is removed.
 *
 * Assumes that the process lock is held.
 */
struct kfd_process_device *kfd_bind_process_to_device(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int err;

	pdd = kfd_get_process_device_data(dev, p);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		return ERR_PTR(-ENOMEM);
	}

	err = kfd_iommu_bind_process_to_device(pdd);
	if (err)
		return ERR_PTR(err);

	return pdd;
}

struct kfd_process_device *kfd_get_first_process_device_data(
						struct kfd_process *p)
{
	return list_first_entry(&p->per_device_data,
				struct kfd_process_device,
				per_device_list);
}

struct kfd_process_device *kfd_get_next_process_device_data(
						struct kfd_process *p,
						struct kfd_process_device *pdd)
{
	if (list_is_last(&pdd->per_device_list, &p->per_device_data))
		return NULL;
	return list_next_entry(pdd, per_device_list);
}

bool kfd_has_process_device_data(struct kfd_process *p)
{
	return !(list_empty(&p->per_device_data));
}

/* This increments the process->ref counter. */
struct kfd_process *kfd_lookup_process_by_pasid(unsigned int pasid)
{
	struct kfd_process *p, *ret_p = NULL;
	unsigned int temp;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (p->pasid == pasid) {
			kref_get(&p->ref);
			ret_p = p;
			break;
		}
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return ret_p;
}

/* This increments the process->ref counter. */
struct kfd_process *kfd_lookup_process_by_mm(const struct mm_struct *mm)
{
	struct kfd_process *p;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	p = find_process_by_mm(mm);
	if (p)
		kref_get(&p->ref);

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}

/* process_evict_queues - Evict all user queues of a process
 *
 * Eviction is reference-counted per process-device. This means multiple
 * evictions from different sources can be nested safely.
 */
static int process_evict_queues(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int r = 0;
	unsigned int n_evicted = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		r = pdd->dev->dqm->ops.evict_process_queues(pdd->dev->dqm,
							    &pdd->qpd);
		if (r) {
			pr_err("Failed to evict process queues\n");
			goto fail;
		}
		n_evicted++;
	}

	return r;

fail:
	/* To keep state consistent, roll back partial eviction by
	 * restoring queues
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		if (n_evicted == 0)
			break;
		if (pdd->dev->dqm->ops.restore_process_queues(pdd->dev->dqm,
							      &pdd->qpd))
			pr_err("Failed to restore queues\n");

		n_evicted--;
	}

	return r;
}

/* process_restore_queues - Restore all user queues of a process */
static  int process_restore_queues(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int r, ret = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		r = pdd->dev->dqm->ops.restore_process_queues(pdd->dev->dqm,
							      &pdd->qpd);
		if (r) {
			pr_err("Failed to restore process queues\n");
			if (!ret)
				ret = r;
		}
	}

	return ret;
}

static void evict_process_worker(struct work_struct *work)
{
	int ret;
	struct kfd_process *p;
	struct delayed_work *dwork;

	dwork = to_delayed_work(work);

	/* Process termination destroys this worker thread. So during the
	 * lifetime of this thread, kfd_process p will be valid
	 */
	p = container_of(dwork, struct kfd_process, eviction_work);
	WARN_ONCE(p->last_eviction_seqno != p->ef->seqno,
		  "Eviction fence mismatch\n");

	/* Narrow window of overlap between restore and evict work
	 * item is possible. Once amdgpu_amdkfd_gpuvm_restore_process_bos
	 * unreserves KFD BOs, it is possible to evicted again. But
	 * restore has few more steps of finish. So lets wait for any
	 * previous restore work to complete
	 */
	flush_delayed_work(&p->restore_work);

	pr_debug("Started evicting pasid %d\n", p->pasid);
	ret = process_evict_queues(p);
	if (!ret) {
		dma_fence_signal(p->ef);
		dma_fence_put(p->ef);
		p->ef = NULL;
		schedule_delayed_work(&p->restore_work,
				msecs_to_jiffies(PROCESS_RESTORE_TIME_MS));

		pr_debug("Finished evicting pasid %d\n", p->pasid);
	} else
		pr_err("Failed to evict queues of pasid %d\n", p->pasid);
}

static void restore_process_worker(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct kfd_process *p;
	struct kfd_process_device *pdd;
	int ret = 0;

	dwork = to_delayed_work(work);

	/* Process termination destroys this worker thread. So during the
	 * lifetime of this thread, kfd_process p will be valid
	 */
	p = container_of(dwork, struct kfd_process, restore_work);

	/* Call restore_process_bos on the first KGD device. This function
	 * takes care of restoring the whole process including other devices.
	 * Restore can fail if enough memory is not available. If so,
	 * reschedule again.
	 */
	pdd = list_first_entry(&p->per_device_data,
			       struct kfd_process_device,
			       per_device_list);

	pr_debug("Started restoring pasid %d\n", p->pasid);

	/* Setting last_restore_timestamp before successful restoration.
	 * Otherwise this would have to be set by KGD (restore_process_bos)
	 * before KFD BOs are unreserved. If not, the process can be evicted
	 * again before the timestamp is set.
	 * If restore fails, the timestamp will be set again in the next
	 * attempt. This would mean that the minimum GPU quanta would be
	 * PROCESS_ACTIVE_TIME_MS - (time to execute the following two
	 * functions)
	 */

	p->last_restore_timestamp = get_jiffies_64();
	ret = pdd->dev->kfd2kgd->restore_process_bos(p->kgd_process_info,
						     &p->ef);
	if (ret) {
		pr_debug("Failed to restore BOs of pasid %d, retry after %d ms\n",
			 p->pasid, PROCESS_BACK_OFF_TIME_MS);
		ret = schedule_delayed_work(&p->restore_work,
				msecs_to_jiffies(PROCESS_BACK_OFF_TIME_MS));
		WARN(!ret, "reschedule restore work failed\n");
		return;
	}

	ret = process_restore_queues(p);
	if (!ret)
		pr_debug("Finished restoring pasid %d\n", p->pasid);
	else
		pr_err("Failed to restore queues of pasid %d\n", p->pasid);
}

void kfd_suspend_all_processes(void)
{
	struct kfd_process *p;
	unsigned int temp;
	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		cancel_delayed_work_sync(&p->eviction_work);
		cancel_delayed_work_sync(&p->restore_work);

		if (process_evict_queues(p))
			pr_err("Failed to suspend process %d\n", p->pasid);
		dma_fence_signal(p->ef);
		dma_fence_put(p->ef);
		p->ef = NULL;
	}
	srcu_read_unlock(&kfd_processes_srcu, idx);
}

int kfd_resume_all_processes(void)
{
	struct kfd_process *p;
	unsigned int temp;
	int ret = 0, idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (!schedule_delayed_work(&p->restore_work, 0)) {
			pr_err("Restore process %d failed during resume\n",
			       p->pasid);
			ret = -EFAULT;
		}
	}
	srcu_read_unlock(&kfd_processes_srcu, idx);
	return ret;
}

int kfd_reserved_mem_mmap(struct kfd_process *process,
			  struct vm_area_struct *vma)
{
	struct kfd_dev *dev = kfd_device_by_id(vma->vm_pgoff);
	struct kfd_process_device *pdd;
	struct qcm_process_device *qpd;

	if (!dev)
		return -EINVAL;
	if ((vma->vm_end - vma->vm_start) != KFD_CWSR_TBA_TMA_SIZE) {
		pr_err("Incorrect CWSR mapping size.\n");
		return -EINVAL;
	}

	pdd = kfd_get_process_device_data(dev, process);
	if (!pdd)
		return -EINVAL;
	qpd = &pdd->qpd;

	qpd->cwsr_kaddr = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(KFD_CWSR_TBA_TMA_SIZE));
	if (!qpd->cwsr_kaddr) {
		pr_err("Error allocating per process CWSR buffer.\n");
		return -ENOMEM;
	}

	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND
		| VM_NORESERVE | VM_DONTDUMP | VM_PFNMAP;
	/* Mapping pages to user process */
	return remap_pfn_range(vma, vma->vm_start,
			       PFN_DOWN(__pa(qpd->cwsr_kaddr)),
			       KFD_CWSR_TBA_TMA_SIZE, vma->vm_page_prot);
}

void kfd_flush_tlb(struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;
	const struct kfd2kgd_calls *f2g = dev->kfd2kgd;

	if (dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		/* Nothing to flush until a VMID is assigned, which
		 * only happens when the first queue is created.
		 */
		if (pdd->qpd.vmid)
			f2g->invalidate_tlbs_vmid(dev->kgd, pdd->qpd.vmid);
	} else {
		f2g->invalidate_tlbs(dev->kgd, pdd->process->pasid);
	}
}

#if defined(CONFIG_DEBUG_FS)

int kfd_debugfs_mqds_by_process(struct seq_file *m, void *data)
{
	struct kfd_process *p;
	unsigned int temp;
	int r = 0;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		seq_printf(m, "Process %d PASID %d:\n",
			   p->lead_thread->tgid, p->pasid);

		mutex_lock(&p->mutex);
		r = pqm_debugfs_mqds(m, &p->pqm);
		mutex_unlock(&p->mutex);

		if (r)
			break;
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return r;
}

#endif
