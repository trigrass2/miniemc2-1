/**
 * @file
 * Analogy for Linux, buffer related features
 *
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __ANALOGY_BUFFER_H__
#define __ANALOGY_BUFFER_H__

#ifndef DOXYGEN_CPP

#ifdef __KERNEL__

#include <linux/version.h>
#include <linux/mm.h>

#include <rtdm/rtdm_driver.h>

#include <analogy/os_facilities.h>
#include <analogy/context.h>

/* --- Events bits / flags --- */

#define A4L_BUF_EOBUF_NR 0
#define A4L_BUF_EOBUF (1 << A4L_BUF_EOBUF_NR)

#define A4L_BUF_ERROR_NR 1
#define A4L_BUF_ERROR (1 << A4L_BUF_ERROR_NR)

#define A4L_BUF_EOA_NR 2
#define A4L_BUF_EOA (1 << A4L_BUF_EOA_NR)

/* --- Status bits / flags --- */

#define A4L_BUF_BULK_NR 8
#define A4L_BUF_BULK (1 << A4L_BUF_BULK_NR)

#define A4L_BUF_MAP_NR 9
#define A4L_BUF_MAP (1 << A4L_BUF_MAP_NR)

struct a4l_subdevice;

/* Buffer descriptor structure */
struct a4l_buffer {

	/* Added by the structure update */
	struct a4l_subdevice *subd;

	/* Buffer's first virtual page pointer */
	void *buf;

	/* Buffer's global size */
	unsigned long size;
	/* Tab containing buffer's pages pointers */
	unsigned long *pg_list;

	/* RT/NRT synchronization element */
	a4l_sync_t sync;

	/* Counters needed for transfer */
	unsigned long end_count;
	unsigned long prd_count;
	unsigned long cns_count;
	unsigned long tmp_count;

	/* Status + events occuring during transfer */
	unsigned long flags;

	/* Command on progress */
	a4l_cmd_t *cur_cmd;

	/* Munge counter */
	unsigned long mng_count;
};
typedef struct a4l_buffer a4l_buf_t;

/* --- Static inline functions related with
   user<->kernel data transfers --- */

/* The function __produce is an inline function which copies data into
   the asynchronous buffer and takes care of the non-contiguous issue
   when looping. This function is used in read and write operations */
static inline int __produce(a4l_cxt_t *cxt,
			    a4l_buf_t *buf, void *pin, unsigned long count)
{
	unsigned long start_ptr = (buf->prd_count % buf->size);
	unsigned long tmp_cnt = count;
	int ret = 0;

	while (ret == 0 && tmp_cnt != 0) {
		/* Check the data copy can be performed contiguously */
		unsigned long blk_size = (start_ptr + tmp_cnt > buf->size) ?
			buf->size - start_ptr : tmp_cnt;

		/* Perform the copy */
		if (cxt == NULL)
			memcpy(buf->buf + start_ptr, pin, blk_size);
		else
			ret = rtdm_safe_copy_from_user(cxt->user_info,
						       buf->buf + start_ptr,
						       pin, blk_size);

		/* Update pointers/counts */
		pin += blk_size;
		tmp_cnt -= blk_size;
		start_ptr = 0;
	}

	return ret;
}

/* The function __consume is an inline function which copies data from
   the asynchronous buffer and takes care of the non-contiguous issue
   when looping. This function is used in read and write operations */
static inline int __consume(a4l_cxt_t *cxt,
			    a4l_buf_t *buf, void *pout, unsigned long count)
{
	unsigned long start_ptr = (buf->cns_count % buf->size);
	unsigned long tmp_cnt = count;
	int ret = 0;

	while (ret == 0 && tmp_cnt != 0) {
		/* Check the data copy can be performed contiguously */
		unsigned long blk_size = (start_ptr + tmp_cnt > buf->size) ?
			buf->size - start_ptr : tmp_cnt;

		/* Perform the copy */
		if (cxt == NULL)
			memcpy(pout, buf->buf + start_ptr, blk_size);
		else
			ret = rtdm_safe_copy_to_user(cxt->user_info,
						     pout,
						     buf->buf + start_ptr,
						     blk_size);

		/* Update pointers/counts */
		pout += blk_size;
		tmp_cnt -= blk_size;
		start_ptr = 0;
	}

	return ret;
}

/* The function __munge is an inline function which calls the
   subdevice specific munge callback on contiguous windows within the
   whole buffer. This function is used in read and write operations */
static inline void __munge(struct a4l_subdevice * subd,
			   void (*munge) (struct a4l_subdevice *,
					  void *, unsigned long),
			   a4l_buf_t * buf, unsigned long count)
{
	unsigned long start_ptr = (buf->mng_count % buf->size);
	unsigned long tmp_cnt = count;

	while (tmp_cnt != 0) {
		/* Check the data copy can be performed contiguously */
		unsigned long blk_size = (start_ptr + tmp_cnt > buf->size) ?
			buf->size - start_ptr : tmp_cnt;

		/* Perform the munge operation */
		munge(subd, buf->buf + start_ptr, blk_size);

		/* Update the start pointer and the count */
		tmp_cnt -= blk_size;
		start_ptr = 0;
	}
}

/* The function __handle_event can only be called from process context
   (not interrupt service routine). It allows the client process to
   retrieve the buffer status which has been updated by the driver */
static inline int __handle_event(a4l_buf_t * buf)
{
	int ret = 0;

	/* The event "End of acquisition" must not be cleaned
	   before the complete flush of the buffer */
	if (test_bit(A4L_BUF_EOA_NR, &buf->flags)) {
		ret = -ENOENT;
	}

	if (test_bit(A4L_BUF_ERROR_NR, &buf->flags)) {
		ret = -EPIPE;
	}

	return ret;
}

/* --- Counters management functions --- */

/* Here, we may wonder why we need more than two counters / pointers.

   Theoretically, we only need two counters (or two pointers):
   - one which tells where the reader should be within the buffer
   - one which tells where the writer should be within the buffer

   With these two counters (or pointers), we just have to check that
   the writer does not overtake the reader inside the ring buffer
   BEFORE any read / write operations.

   However, if one element is a DMA controller, we have to be more
   careful. Generally a DMA transfer occurs like this:
   DMA shot
      |-> then DMA interrupt
	 |-> then DMA soft handler which checks the counter

   So, the checkings occur AFTER the write operations.

   Let's take an example: the reader is a software task and the writer
   is a DMA controller. At the end of the DMA shot, the write counter
   is higher than the read counter. Unfortunately, a read operation
   occurs between the DMA shot and the DMA interrupt, so the handler
   will not notice that an overflow occured.

   That is why tmp_count comes into play: tmp_count records the
   read/consumer current counter before the next DMA shot and once the
   next DMA shot is done, we check that the updated writer/producer
   counter is not higher than tmp_count. Thus we are sure that the DMA
   writer has not overtaken the reader because it was not able to
   overtake the n-1 value. */

static inline int __pre_abs_put(a4l_buf_t * buf, unsigned long count)
{
	if (count - buf->tmp_count > buf->size) {
		set_bit(A4L_BUF_ERROR_NR, &buf->flags);
		return -EPIPE;
	}

	buf->tmp_count = buf->cns_count;

	return 0;
}

static inline int __pre_put(a4l_buf_t * buf, unsigned long count)
{
	return __pre_abs_put(buf, buf->tmp_count + count);
}

static inline int __pre_abs_get(a4l_buf_t * buf, unsigned long count)
{
	/* The first time, we expect the buffer to be properly filled
	before the trigger occurence; by the way, we need tmp_count to
	have been initialized and tmp_count is updated right here */
	if (buf->tmp_count == 0 || buf->cns_count == 0)
		goto out;

	/* At the end of the acquisition, the user application has
	written the defined amount of data into the buffer; so the
	last time, the DMA channel can easily overtake the tmp
	frontier because no more data were sent from user space;
	therefore no useless alarm should be sent */
	if (buf->end_count != 0 && (long)(count - buf->end_count) > 0)
		goto out;

	/* Once the exception are passed, we check that the DMA
	transfer has not overtaken the last record of the production
	count (tmp_count was updated with prd_count the last time
	__pre_abs_get was called). We must understand that we cannot
	compare the current DMA count with the current production
	count because even if, right now, the production count is
	higher than the DMA count, it does not mean that the DMA count
	was not greater a few cycles before; in such case, the DMA
	channel would have retrieved the wrong data */
	if ((long)(count - buf->tmp_count) > 0) {
		set_bit(A4L_BUF_ERROR_NR, &buf->flags);
		return -EPIPE;
	}

out:
	buf->tmp_count = buf->prd_count;

	return 0;
}

static inline int __pre_get(a4l_buf_t * buf, unsigned long count)
{
	return __pre_abs_get(buf, buf->tmp_count + count);
}

static inline int __abs_put(a4l_buf_t * buf, unsigned long count)
{
	unsigned long old = buf->prd_count;

	if ((long)(buf->prd_count - count) >= 0)
		return -EINVAL;

	buf->prd_count = count;

	if ((old / buf->size) != (count / buf->size))
		set_bit(A4L_BUF_EOBUF_NR, &buf->flags);

	if (buf->end_count != 0 && (long)(count - buf->end_count) >= 0)
		set_bit(A4L_BUF_EOA_NR, &buf->flags);

	return 0;
}

static inline int __put(a4l_buf_t * buf, unsigned long count)
{
	return __abs_put(buf, buf->prd_count + count);
}

static inline int __abs_get(a4l_buf_t * buf, unsigned long count)
{
	unsigned long old = buf->cns_count;

	if ((long)(buf->cns_count - count) >= 0)
		return -EINVAL;

	buf->cns_count = count;

	if ((old / buf->size) != count / buf->size)
		set_bit(A4L_BUF_EOBUF_NR, &buf->flags);

	if (buf->end_count != 0 && (long)(count - buf->end_count) >= 0)
		set_bit(A4L_BUF_EOA_NR, &buf->flags);

	return 0;
}

static inline int __get(a4l_buf_t * buf, unsigned long count)
{
	return __abs_get(buf, buf->cns_count + count);
}

static inline unsigned long __count_to_put(a4l_buf_t * buf)
{
	unsigned long ret;

	if ((long) (buf->size + buf->cns_count - buf->prd_count) > 0)
		ret = buf->size + buf->cns_count - buf->prd_count;
	else
		ret = 0;

	return ret;
}

static inline unsigned long __count_to_get(a4l_buf_t * buf)
{
	unsigned long ret;

	/* If the acquisition is unlimited (end_count == 0), we must
	   not take into account end_count */
	if (buf->end_count == 0 || (long)(buf->end_count - buf->prd_count) > 0)
		ret = buf->prd_count;
	else
		ret = buf->end_count;

	if ((long)(ret - buf->cns_count) > 0)
		ret -= buf->cns_count;
	else
		ret = 0;

	return ret;
}

/* --- Buffer internal functions --- */

int a4l_alloc_buffer(a4l_buf_t *buf_desc, int buf_size);

void a4l_free_buffer(a4l_buf_t *buf_desc);

void a4l_init_buffer(a4l_buf_t * buf_desc);

void a4l_cleanup_buffer(a4l_buf_t * buf_desc);

int a4l_setup_buffer(a4l_cxt_t *cxt, a4l_cmd_t *cmd);

int a4l_cancel_buffer(a4l_cxt_t *cxt);

int a4l_buf_prepare_absput(struct a4l_subdevice *subd,
			   unsigned long count);

int a4l_buf_commit_absput(struct a4l_subdevice *subd,
			  unsigned long count);

int a4l_buf_prepare_put(struct a4l_subdevice *subd,
			unsigned long count);

int a4l_buf_commit_put(struct a4l_subdevice *subd,
		       unsigned long count);

int a4l_buf_put(struct a4l_subdevice *subd,
		void *bufdata, unsigned long count);

int a4l_buf_prepare_absget(struct a4l_subdevice *subd,
			   unsigned long count);

int a4l_buf_commit_absget(struct a4l_subdevice *subd,
			  unsigned long count);

int a4l_buf_prepare_get(struct a4l_subdevice *subd,
			unsigned long count);

int a4l_buf_commit_get(struct a4l_subdevice *subd,
		       unsigned long count);

int a4l_buf_get(struct a4l_subdevice *subd,
		void *bufdata, unsigned long count);

int a4l_buf_evt(struct a4l_subdevice *subd, unsigned long evts);

unsigned long a4l_buf_count(struct a4l_subdevice *subd);

/* --- Current Command management function --- */

static inline a4l_cmd_t *a4l_get_cmd(a4l_subd_t *subd)
{
	return (subd->buf) ? subd->buf->cur_cmd : NULL;
}

/* --- Munge related function --- */

int a4l_get_chan(struct a4l_subdevice *subd);

/* --- IOCTL / FOPS functions --- */

int a4l_ioctl_mmap(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_bufcfg(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_bufinfo(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_poll(a4l_cxt_t * cxt, void *arg);
ssize_t a4l_read_buffer(a4l_cxt_t * cxt, void *bufdata, size_t nbytes);
ssize_t a4l_write_buffer(a4l_cxt_t * cxt, const void *bufdata, size_t nbytes);
int a4l_select(a4l_cxt_t *cxt,
	       rtdm_selector_t *selector,
	       enum rtdm_selecttype type, unsigned fd_index);

#endif /* __KERNEL__ */

/* MMAP ioctl argument structure */
struct a4l_mmap_arg {
	unsigned int idx_subd;
	unsigned long size;
	void *ptr;
};
typedef struct a4l_mmap_arg a4l_mmap_t;

/* Constants related with buffer size
   (might be used with BUFCFG ioctl) */
#define A4L_BUF_MAXSIZE 0x1000000
#define A4L_BUF_DEFSIZE 0x10000
#define A4L_BUF_DEFMAGIC 0xffaaff55

/* BUFCFG ioctl argument structure */
struct a4l_buffer_config {
	/* NOTE: with the last buffer implementation, the field
	   idx_subd became useless; the buffer are now
	   per-context. So, the buffer size configuration is specific
	   to an opened device. There is a little exception: we can
	   define a default buffer size for a device.
	   So far, a hack is used to implement the configuration of
	   the default buffer size */
	unsigned int idx_subd;
	unsigned long buf_size;
};
typedef struct a4l_buffer_config a4l_bufcfg_t;

/* BUFINFO ioctl argument structure */
struct a4l_buffer_info {
	unsigned int idx_subd;
	unsigned long buf_size;
	unsigned long rw_count;
};
typedef struct a4l_buffer_info a4l_bufinfo_t;

/* POLL ioctl argument structure */
struct a4l_poll {
	unsigned int idx_subd;
	unsigned long arg;
};
typedef struct a4l_poll a4l_poll_t;

#endif /* !DOXYGEN_CPP */

#endif /* __ANALOGY_BUFFER_H__ */
